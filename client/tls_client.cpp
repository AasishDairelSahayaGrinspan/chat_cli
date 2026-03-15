#include "tls_client.hpp"
#include "logging/logger.hpp"

#include <openssl/ssl.h>

namespace chat::client {

TlsClient::TlsClient(const std::string& host, uint16_t port, bool verify_ssl, const std::string& ca_cert_path)
    : host_(host),
      port_(port),
      verify_ssl_(verify_ssl),
      ca_cert_path_(ca_cert_path),
      ssl_context_(asio::ssl::context::tls_client),
      resolver_(io_context_) {

    // Disable old protocols
    ssl_context_.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1
    );

    if (verify_ssl_) {
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
        if (!ca_cert_path_.empty()) {
            ssl_context_.load_verify_file(ca_cert_path_);
        } else {
            ssl_context_.set_default_verify_paths();
        }
        ssl_context_.set_verify_callback(asio::ssl::host_name_verification(host_));
    } else {
        ssl_context_.set_verify_mode(asio::ssl::verify_none);
    }
}

TlsClient::~TlsClient() {
    stop();
}

void TlsClient::connect(ConnectHandler handler) {
    connect_handler_ = std::move(handler);
    reconnect_attempts_ = 0;

    // Reset socket
    socket_ = std::make_unique<ssl_socket>(io_context_, ssl_context_);

    // Resolve host
    resolver_.async_resolve(
        host_, std::to_string(port_),
        [this](const std::error_code& ec, tcp::resolver::results_type results) {
            if (ec) {
                if (connect_handler_) {
                    connect_handler_(false, "Failed to resolve host: " + ec.message());
                }
                handle_disconnect("Resolution failed");
                return;
            }
            do_connect(results);
        });
}

void TlsClient::do_connect(tcp::resolver::results_type endpoints) {
    asio::async_connect(
        socket_->lowest_layer(), endpoints,
        [this](const std::error_code& ec, const tcp::endpoint&) {
            if (ec) {
                if (connect_handler_) {
                    connect_handler_(false, "Connection failed: " + ec.message());
                }
                handle_disconnect("Connection failed");
                return;
            }
            do_handshake();
        });
}

void TlsClient::do_handshake() {
    // Set SNI hostname
    SSL_set_tlsext_host_name(socket_->native_handle(), host_.c_str());

    socket_->async_handshake(
        asio::ssl::stream_base::client,
        [this](const std::error_code& ec) {
            if (ec) {
                if (connect_handler_) {
                    connect_handler_(false, "TLS handshake failed: " + ec.message());
                }
                handle_disconnect("Handshake failed");
                return;
            }

            connected_ = true;
            reconnect_attempts_ = 0;

            if (connect_handler_) {
                connect_handler_(true, "");
            }

            do_read_header();
        });
}

void TlsClient::do_read_header() {
    if (!connected_ || stopping_) return;

    asio::async_read(
        *socket_,
        asio::buffer(header_buffer_),
        [this](const std::error_code& ec, size_t) {
            if (ec) {
                handle_disconnect(ec.message());
                return;
            }

            uint32_t length = protocol::Framing::read_length(header_buffer_.data());

            if (length > protocol::MAX_MESSAGE_SIZE) {
                handle_disconnect("Server sent oversized message");
                return;
            }

            do_read_body(length);
        });
}

void TlsClient::do_read_body(uint32_t length) {
    if (!connected_ || stopping_) return;

    body_buffer_.resize(length);

    asio::async_read(
        *socket_,
        asio::buffer(body_buffer_),
        [this](const std::error_code& ec, size_t) {
            if (ec) {
                handle_disconnect(ec.message());
                return;
            }

            try {
                std::string json_str(body_buffer_.begin(), body_buffer_.end());
                auto j = nlohmann::json::parse(json_str);
                auto msg = j.get<protocol::Message>();

                if (message_handler_) {
                    message_handler_(msg);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse message: {}", e.what());
            }

            do_read_header();
        });
}

void TlsClient::send(const protocol::Message& msg) {
    if (!connected_) return;

    try {
        auto frame = protocol::Framing::encode(msg);

        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push_back(std::move(frame));

        if (!writing_) {
            writing_ = true;
            do_write();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to encode message: {}", e.what());
    }
}

void TlsClient::do_write() {
    if (!connected_ || stopping_) return;

    asio::async_write(
        *socket_,
        asio::buffer(write_queue_.front()),
        [this](const std::error_code& ec, size_t) {
            std::lock_guard<std::mutex> lock(write_mutex_);

            if (ec) {
                writing_ = false;
                return;
            }

            write_queue_.pop_front();

            if (!write_queue_.empty()) {
                do_write();
            } else {
                writing_ = false;
            }
        });
}

void TlsClient::send_command(const std::string& cmd, const nlohmann::json& payload) {
    auto msg = protocol::Message::create(protocol::MessageType::COMMAND);
    msg.payload = payload;
    msg.payload["command"] = cmd;
    send(msg);
}

void TlsClient::send_chat(const std::string& message, const std::string& room) {
    auto msg = protocol::Message::create(protocol::MessageType::MESSAGE, "", room);
    msg.payload = {{"message", message}};
    send(msg);
}

void TlsClient::disconnect() {
    if (!connected_) return;

    connected_ = false;

    std::error_code ec;
    if (socket_) {
        socket_->lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
        socket_->lowest_layer().close(ec);
    }
}

void TlsClient::handle_disconnect(const std::string& reason) {
    bool was_connected = connected_.exchange(false);

    if (was_connected && disconnect_handler_) {
        disconnect_handler_(reason);
    }

    if (reconnect_enabled_ && !stopping_ &&
        reconnect_attempts_ < max_reconnect_attempts_) {
        do_reconnect();
    }
}

void TlsClient::do_reconnect() {
    ++reconnect_attempts_;

    LOG_INFO("Reconnecting (attempt {}/{})", reconnect_attempts_, max_reconnect_attempts_);

    reconnect_timer_ = std::make_unique<asio::steady_timer>(io_context_);
    reconnect_timer_->expires_after(std::chrono::milliseconds(reconnect_delay_ms_));
    reconnect_timer_->async_wait([this](const std::error_code& ec) {
        if (!ec && !stopping_) {
            socket_ = std::make_unique<ssl_socket>(io_context_, ssl_context_);

            resolver_.async_resolve(
                host_, std::to_string(port_),
                [this](const std::error_code& ec,
                       tcp::resolver::results_type results) {
                    if (!ec) {
                        do_connect(results);
                    } else if (reconnect_attempts_ < max_reconnect_attempts_) {
                        do_reconnect();
                    }
                });
        }
    });
}

void TlsClient::enable_reconnect(size_t max_attempts, size_t delay_ms) {
    reconnect_enabled_ = true;
    max_reconnect_attempts_ = max_attempts;
    reconnect_delay_ms_ = delay_ms;
}

void TlsClient::disable_reconnect() {
    reconnect_enabled_ = false;
}

void TlsClient::run() {
    if (running_.exchange(true)) return;

    io_thread_ = std::thread([this]() {
        auto work = asio::make_work_guard(io_context_);
        io_context_.run();
    });
}

void TlsClient::stop() {
    if (stopping_.exchange(true)) return;

    disconnect();
    io_context_.stop();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    running_ = false;
}

} // namespace chat::client

