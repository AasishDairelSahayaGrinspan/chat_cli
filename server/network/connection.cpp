#include "connection.hpp"
#include "connection_manager.hpp"
#include "logging/logger.hpp"

namespace chat::server {

std::atomic<uint64_t> Connection::next_id_{1};

Connection::Connection(asio::io_context& io_context,
                      asio::ssl::context& ssl_context,
                      ConnectionManager& manager)
    : id_(next_id_++),
      socket_(io_context, ssl_context),
      manager_(manager) {
}

Connection::~Connection() {
    LOG_DEBUG("Connection {} destroyed", id_);
}

void Connection::start() {
    try {
        auto endpoint = socket_.lowest_layer().remote_endpoint();
        remote_address_ = endpoint.address().to_string();
        remote_port_ = endpoint.port();
    } catch (const std::exception& e) {
        LOG_WARN("Failed to get remote endpoint: {}", e.what());
    }

    LOG_INFO("Connection {} from {}:{}", id_, remote_address_, remote_port_);
    do_handshake();
}

void Connection::do_handshake() {
    auto self = shared_from_this();
    socket_.async_handshake(
        asio::ssl::stream_base::server,
        [this, self](const std::error_code& ec) {
            if (ec) {
                LOG_WARN("TLS handshake failed for connection {}: {}", id_, ec.message());
                if (error_handler_) {
                    error_handler_(self, ec);
                }
                return;
            }
            LOG_DEBUG("TLS handshake complete for connection {}", id_);
            do_read_header();
        });
}

void Connection::do_read_header() {
    if (closed_) return;

    auto self = shared_from_this();
    asio::async_read(
        socket_,
        asio::buffer(header_buffer_),
        [this, self](const std::error_code& ec, size_t /*bytes*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    LOG_DEBUG("Connection {} read error: {}", id_, ec.message());
                }
                if (error_handler_) {
                    error_handler_(self, ec);
                }
                return;
            }

            uint32_t length = protocol::Framing::read_length(header_buffer_.data());

            if (length > protocol::MAX_MESSAGE_SIZE) {
                LOG_WARN("Connection {} sent oversized message: {} bytes", id_, length);
                send(protocol::Message::error("Message too large", "SIZE_LIMIT"));
                close();
                return;
            }

            do_read_body(length);
        });
}

void Connection::do_read_body(uint32_t length) {
    if (closed_) return;

    body_buffer_.resize(length);
    auto self = shared_from_this();

    asio::async_read(
        socket_,
        asio::buffer(body_buffer_),
        [this, self](const std::error_code& ec, size_t /*bytes*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    LOG_DEBUG("Connection {} body read error: {}", id_, ec.message());
                }
                if (error_handler_) {
                    error_handler_(self, ec);
                }
                return;
            }

            try {
                std::string json_str(body_buffer_.begin(), body_buffer_.end());
                auto j = nlohmann::json::parse(json_str);
                auto msg = j.get<protocol::Message>();

                if (message_handler_) {
                    message_handler_(self, msg);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Connection {} invalid message: {}", id_, e.what());
                send(protocol::Message::error("Invalid message format", "PARSE_ERROR"));
            }

            do_read_header();
        });
}

void Connection::send(const protocol::Message& msg) {
    try {
        auto frame = protocol::Framing::encode(msg);
        send_raw(frame);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to encode message: {}", e.what());
    }
}

void Connection::send_raw(const std::vector<uint8_t>& data) {
    if (closed_) return;

    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push_back(data);

    if (!writing_) {
        writing_ = true;
        do_write();
    }
}

void Connection::do_write() {
    if (closed_) return;

    auto self = shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(write_queue_.front()),
        [this, self](const std::error_code& ec, size_t /*bytes*/) {
            std::lock_guard<std::mutex> lock(write_mutex_);

            if (ec) {
                LOG_DEBUG("Connection {} write error: {}", id_, ec.message());
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

void Connection::close() {
    if (closed_.exchange(true)) {
        return;  // Already closed
    }

    LOG_DEBUG("Closing connection {}", id_);

    std::error_code ec;
    socket_.lowest_layer().shutdown(tcp::socket::shutdown_both, ec);
    socket_.lowest_layer().close(ec);
}

} // namespace chat::server

