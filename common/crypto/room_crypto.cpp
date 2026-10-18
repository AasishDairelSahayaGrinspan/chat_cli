#include "crypto/room_crypto.hpp"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <cctype>
#include <mutex>
#include <stdexcept>

namespace chat::crypto {

namespace {

void ensure_sodium() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
    });
}

bool is_valid_b64_chars(const std::string& s) {
    if (s.empty() || s.size() > 4096) {
        return false;
    }
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string sign_input_for_message(const std::string& aad,
                                   const std::string& nonce_b64,
                                   const std::string& ciphertext_b64) {
    return aad + "|" + nonce_b64 + "|" + ciphertext_b64;
}

std::string sign_input_for_envelope(const std::string& room,
                                    const std::string& sender,
                                    const std::string& recipient,
                                    uint64_t epoch,
                                    const std::string& key_id,
                                    const std::string& nonce_b64,
                                    const std::string& box_b64) {
    return "cli-chat-env/v2|" + room + "|" + sender + "|" + recipient + "|" +
           std::to_string(epoch) + "|" + key_id + "|" + nonce_b64 + "|" + box_b64;
}

} // namespace

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

std::string to_b64(const unsigned char* data, size_t len) {
    ensure_sodium();
    if (data == nullptr && len != 0) {
        return "";
    }
    size_t b64_len = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string result(b64_len, '\0');
    sodium_bin2base64(&result[0], b64_len, data, len, sodium_base64_VARIANT_ORIGINAL);
    while (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

std::string to_b64(const Bytes& data) {
    return to_b64(data.data(), data.size());
}

Bytes from_b64(const std::string& b64) {
    ensure_sodium();
    if (!is_valid_b64_chars(b64)) {
        return {};
    }

    Bytes result(b64.size());
    size_t bin_len = 0;
    if (sodium_base642bin(result.data(), result.size(),
                          b64.c_str(), b64.size(),
                          nullptr, &bin_len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        return {};
    }
    result.resize(bin_len);
    return result;
}

std::string new_key_id() {
    ensure_sodium();
    unsigned char raw[4];
    randombytes_buf(raw, sizeof(raw));
    char hex[9];
    sodium_bin2hex(hex, sizeof(hex), raw, sizeof(raw));
    return std::string(hex, 8);
}

bool is_valid_key_id(const std::string& key_id) {
    if (key_id.size() != 8) {
        return false;
    }
    for (char c : key_id) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Ratchet
// ---------------------------------------------------------------------------

RatchetStep derive_ratchet_step(const Bytes& chain_key) {
    ensure_sodium();

    RatchetStep step;
    if (chain_key.size() != ROOM_KEY_BYTES) {
        return step;
    }

    Bytes buf;
    buf.reserve(chain_key.size() + 1);

    buf.push_back(0x01);
    buf.insert(buf.end(), chain_key.begin(), chain_key.end());
    step.message_key.resize(32);
    crypto_generichash(step.message_key.data(), step.message_key.size(),
                       buf.data(), buf.size(), nullptr, 0);

    buf[0] = 0x02;
    step.next_chain.resize(32);
    crypto_generichash(step.next_chain.data(), step.next_chain.size(),
                       buf.data(), buf.size(), nullptr, 0);

    return step;
}

// ---------------------------------------------------------------------------
// Fingerprints
// ---------------------------------------------------------------------------

std::string identity_fingerprint(const Bytes& x25519_pk, const Bytes& ed25519_pk) {
    ensure_sodium();

    Bytes buf;
    buf.reserve(x25519_pk.size() + ed25519_pk.size());
    buf.insert(buf.end(), x25519_pk.begin(), x25519_pk.end());
    buf.insert(buf.end(), ed25519_pk.begin(), ed25519_pk.end());

    unsigned char digest[16];
    crypto_generichash(digest, sizeof(digest), buf.data(), buf.size(), nullptr, 0);

    char hex[33];
    sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));

    std::string fp;
    for (int i = 0; i < 32; ++i) {
        if (i > 0 && i % 4 == 0) {
            fp += ' ';
        }
        fp += static_cast<char>(std::toupper(static_cast<unsigned char>(hex[i])));
    }
    return fp;
}

// ---------------------------------------------------------------------------
// AAD
// ---------------------------------------------------------------------------

std::string build_message_aad(const std::string& room, const std::string& sender,
                              uint64_t epoch, const std::string& key_id,
                              uint32_t index) {
    return "cli-chat-e2ee/v2|" + room + "|" + sender + "|" +
           std::to_string(epoch) + "|" + key_id + "|" + std::to_string(index);
}

// ---------------------------------------------------------------------------
// OutboundSenderKey
// ---------------------------------------------------------------------------

OutboundSenderKey::OutboundSenderKey() {
    ensure_sodium();
    chain_.resize(ROOM_KEY_BYTES);
    randombytes_buf(chain_.data(), chain_.size());
    index_ = 0;
    key_id_ = new_key_id();
}

OutboundSenderKey OutboundSenderKey::generate() {
    return OutboundSenderKey{};
}

std::optional<OutboundSenderKey> OutboundSenderKey::from_chain(
    const Bytes& chain, uint32_t index, const std::string& key_id) {
    if (chain.size() != ROOM_KEY_BYTES || !is_valid_key_id(key_id)) {
        return std::nullopt;
    }
    OutboundSenderKey key;
    key.chain_ = chain;
    key.index_ = index;
    key.key_id_ = key_id;
    return key;
}

std::string OutboundSenderKey::serialize_state() const {
    nlohmann::json j{
        {"key_id", key_id_},
        {"chain", to_b64(chain_)},
        {"index", index_},
    };
    return j.dump();
}

void OutboundSenderKey::advance(const Bytes& next_chain) {
    if (next_chain.size() != ROOM_KEY_BYTES) {
        return;
    }
    chain_ = next_chain;
    ++index_;
}

std::optional<OutboundSenderKey> OutboundSenderKey::deserialize_state(
    const std::string& json) {
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    if (!j.contains("key_id") || !j.contains("chain") || !j.contains("index")) {
        return std::nullopt;
    }

    try {
        Bytes chain = from_b64(j.at("chain").get<std::string>());
        uint32_t index = j.at("index").get<uint32_t>();
        std::string key_id = j.at("key_id").get<std::string>();
        return from_chain(chain, index, key_id);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<SealedRoomMessage> seal_room_message(OutboundSenderKey& out,
                                                   uint64_t epoch,
                                                   const std::string& room,
                                                   const std::string& sender,
                                                   const std::string& plaintext,
                                                   const Bytes& sender_ed25519_sk) {
    ensure_sodium();

    if (sender_ed25519_sk.size() != crypto_sign_SECRETKEYBYTES) {
        return std::nullopt;
    }

    RatchetStep step = derive_ratchet_step(out.chain_key());
    if (step.message_key.size() != 32 || step.next_chain.size() != 32) {
        return std::nullopt;
    }

    const uint32_t index = out.next_index();
    const std::string aad = build_message_aad(room, sender, epoch, out.key_id(), index);

    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<unsigned char> ciphertext(plaintext.size() +
                                          crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long ciphertext_len = 0;

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data(), &ciphertext_len,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            plaintext.size(),
            reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
            nullptr, nonce, step.message_key.data()) != 0) {
        return std::nullopt;
    }
    ciphertext.resize(static_cast<size_t>(ciphertext_len));

    SealedRoomMessage sealed;
    sealed.epoch = epoch;
    sealed.key_id = out.key_id();
    sealed.index = index;
    sealed.nonce_b64 = to_b64(nonce, sizeof(nonce));
    sealed.ciphertext_b64 = to_b64(ciphertext);

    const std::string sig_input =
        sign_input_for_message(aad, sealed.nonce_b64, sealed.ciphertext_b64);

    unsigned char sig[crypto_sign_BYTES];
    unsigned long long sig_len = 0;
    if (crypto_sign_detached(sig, &sig_len,
                             reinterpret_cast<const unsigned char*>(sig_input.data()),
                             sig_input.size(),
                             sender_ed25519_sk.data()) != 0) {
        return std::nullopt;
    }
    sealed.signature_b64 = to_b64(sig, sizeof(sig));

    // Success: advance our chain so the message key can never be reused.
    out.advance(step.next_chain);
    secure_wipe(step.message_key);

    return sealed;
}

// ---------------------------------------------------------------------------
// InboundSenderKey
// ---------------------------------------------------------------------------

InboundSenderKey::InboundSenderKey(const Bytes& chain_key) : chain_(chain_key) {}

bool InboundSenderKey::has_index(uint32_t index) const {
    if (index < next_) {
        return skipped_.count(index) > 0;
    }
    return index - next_ <= MAX_SKIPPED_MESSAGE_KEYS;
}

std::optional<Bytes> InboundSenderKey::take_message_key(uint32_t index) {
    if (chain_.size() != ROOM_KEY_BYTES) {
        return std::nullopt;
    }

    // Already-consumed indexes: one-time lookup in the skipped cache.
    if (index < next_) {
        auto it = skipped_.find(index);
        if (it == skipped_.end()) {
            return std::nullopt;  // replay or dropped: reject
        }
        Bytes key = it->second;
        skipped_.erase(it);
        return key;
    }

    // Refuse to fast-forward absurd distances (forces a rekey instead of
    // letting an attacker force us to compute unbounded keys).
    if (index - next_ > MAX_SKIPPED_MESSAGE_KEYS) {
        return std::nullopt;
    }

    // Advance the chain, caching skipped keys for messages we haven't seen.
    while (next_ < index) {
        RatchetStep step = derive_ratchet_step(chain_);
        if (step.message_key.size() != 32) {
            return std::nullopt;
        }
        skipped_[next_] = step.message_key;
        chain_ = step.next_chain;
        ++next_;

        while (skipped_.size() > MAX_SKIPPED_MESSAGE_KEYS) {
            skipped_.erase(skipped_.begin());
        }
    }

    RatchetStep step = derive_ratchet_step(chain_);
    if (step.message_key.size() != 32) {
        return std::nullopt;
    }
    chain_ = step.next_chain;
    ++next_;
    return step.message_key;
}

InboundSenderKey InboundSenderKey::snapshot() const {
    return *this;
}

void InboundSenderKey::restore(const InboundSenderKey& other) {
    *this = other;
}

std::string InboundSenderKey::serialize_state() const {
    nlohmann::json skipped = nlohmann::json::object();
    for (const auto& [idx, key] : skipped_) {
        skipped[std::to_string(idx)] = to_b64(key);
    }
    nlohmann::json j{
        {"chain", to_b64(chain_)},
        {"next", next_},
        {"skipped", skipped},
    };
    return j.dump();
}

std::optional<InboundSenderKey> InboundSenderKey::deserialize_state(
    const std::string& json) {
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    if (!j.contains("chain") || !j.contains("next")) {
        return std::nullopt;
    }

    try {
        Bytes chain = from_b64(j.at("chain").get<std::string>());
        if (chain.size() != ROOM_KEY_BYTES) {
            return std::nullopt;
        }
        InboundSenderKey key(chain);
        key.next_ = j.at("next").get<uint32_t>();

        if (j.contains("skipped") && j.at("skipped").is_object()) {
            for (auto it = j.at("skipped").begin(); it != j.at("skipped").end(); ++it) {
                uint32_t idx = static_cast<uint32_t>(std::stoul(it.key()));
                Bytes k = from_b64(it.value().get<std::string>());
                if (k.size() == 32 && key.skipped_.size() < MAX_SKIPPED_MESSAGE_KEYS) {
                    key.skipped_[idx] = k;
                }
            }
        }
        return key;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> open_room_message(InboundSenderKey& in,
                                             const SealedRoomMessage& sealed,
                                             const std::string& room,
                                             const std::string& sender,
                                             const Bytes& sender_ed25519_pk) {
    ensure_sodium();

    if (sender_ed25519_pk.size() != crypto_sign_PUBLICKEYBYTES) {
        return std::nullopt;
    }

    const std::string aad = build_message_aad(room, sender, sealed.epoch,
                                              sealed.key_id, sealed.index);
    const std::string sig_input =
        sign_input_for_message(aad, sealed.nonce_b64, sealed.ciphertext_b64);

    // 1) Authenticate the sender before touching chain state.
    Bytes sig = from_b64(sealed.signature_b64);
    if (sig.size() != crypto_sign_BYTES ||
        crypto_sign_verify_detached(
            sig.data(),
            reinterpret_cast<const unsigned char*>(sig_input.data()),
            sig_input.size(),
            sender_ed25519_pk.data()) != 0) {
        return std::nullopt;
    }

    Bytes nonce = from_b64(sealed.nonce_b64);
    Bytes ciphertext = from_b64(sealed.ciphertext_b64);
    if (nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES ||
        ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return std::nullopt;
    }

    // 2) Take the one-time message key; roll back if the AEAD fails so a
    // forged message can't burn a legitimate chain step.
    InboundSenderKey before = in.snapshot();
    auto message_key = in.take_message_key(sealed.index);
    if (!message_key.has_value()) {
        return std::nullopt;
    }

    std::vector<unsigned char> plaintext(
        ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long plaintext_len = 0;

    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext.data(), &plaintext_len, nullptr,
            ciphertext.data(), ciphertext.size(),
            reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
            nonce.data(), message_key->data()) != 0) {
        in.restore(before);
        return std::nullopt;
    }

    return std::string(plaintext.begin(), plaintext.begin() + plaintext_len);
}

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------

std::optional<WrappedEnvelope> wrap_sender_key_envelope(
    const std::string& room,
    const std::string& sender,
    const std::string& recipient,
    uint64_t epoch,
    const std::string& key_id,
    const Bytes& chain_key,
    const Bytes& sender_x25519_pk,
    const Bytes& sender_x25519_sk,
    const Bytes& sender_ed25519_sk,
    const Bytes& recipient_x25519_pk) {
    ensure_sodium();

    if (chain_key.size() != ROOM_KEY_BYTES ||
        !is_valid_key_id(key_id) ||
        sender_x25519_pk.size() != crypto_box_PUBLICKEYBYTES ||
        sender_x25519_sk.size() != crypto_box_SECRETKEYBYTES ||
        sender_ed25519_sk.size() != crypto_sign_SECRETKEYBYTES ||
        recipient_x25519_pk.size() != crypto_box_PUBLICKEYBYTES) {
        return std::nullopt;
    }

    unsigned char derived_ed_pk[crypto_sign_PUBLICKEYBYTES];
    crypto_sign_ed25519_sk_to_pk(derived_ed_pk, sender_ed25519_sk.data());
    Bytes ed_public_key(derived_ed_pk, derived_ed_pk + sizeof(derived_ed_pk));

    nlohmann::json inner{
        {"room", room},
        {"epoch", epoch},
        {"key_id", key_id},
        {"chain", to_b64(chain_key)},
        {"x25519", to_b64(sender_x25519_pk)},
        {"ed25519", to_b64(ed_public_key)},
    };
    std::string inner_str = inner.dump();

    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<unsigned char> box(inner_str.size() + crypto_box_MACBYTES);
    if (crypto_box_easy(box.data(),
                        reinterpret_cast<const unsigned char*>(inner_str.data()),
                        inner_str.size(),
                        nonce, recipient_x25519_pk.data(),
                        sender_x25519_sk.data()) != 0) {
        return std::nullopt;
    }

    WrappedEnvelope wrapped;
    wrapped.nonce_b64 = to_b64(nonce, sizeof(nonce));
    wrapped.box_b64 = to_b64(box);
    wrapped.sender_x25519_b64 = to_b64(sender_x25519_pk);
    wrapped.sender_ed25519_b64 = to_b64(ed_public_key);

    const std::string sig_input = sign_input_for_envelope(
        room, sender, recipient, epoch, key_id, wrapped.nonce_b64, wrapped.box_b64);

    unsigned char sig[crypto_sign_BYTES];
    unsigned long long sig_len = 0;
    if (crypto_sign_detached(sig, &sig_len,
                             reinterpret_cast<const unsigned char*>(sig_input.data()),
                             sig_input.size(),
                             sender_ed25519_sk.data()) != 0) {
        return std::nullopt;
    }
    wrapped.signature_b64 = to_b64(sig, sizeof(sig));

    secure_wipe_str(inner_str);
    return wrapped;
}

std::optional<SenderKeyEnvelope> unwrap_sender_key_envelope(
    const std::string& room,
    const std::string& sender,
    const std::string& recipient,
    uint64_t expected_epoch,
    const std::string& expected_key_id,
    const WrappedEnvelope& wrapped,
    const Bytes& own_x25519_sk,
    const Bytes& sender_x25519_pk,
    const Bytes& sender_ed25519_pk) {
    ensure_sodium();

    if (own_x25519_sk.size() != crypto_box_SECRETKEYBYTES ||
        sender_x25519_pk.size() != crypto_box_PUBLICKEYBYTES ||
        sender_ed25519_pk.size() != crypto_sign_PUBLICKEYBYTES ||
        !is_valid_key_id(expected_key_id)) {
        return std::nullopt;
    }

    // Verify the signature against the caller's trusted copy of the
    // sender's key before attempting any decryption.
    const std::string sig_input = sign_input_for_envelope(
        room, sender, recipient, expected_epoch, expected_key_id,
        wrapped.nonce_b64, wrapped.box_b64);

    Bytes sig = from_b64(wrapped.signature_b64);
    if (sig.size() != crypto_sign_BYTES ||
        crypto_sign_verify_detached(
            sig.data(),
            reinterpret_cast<const unsigned char*>(sig_input.data()),
            sig_input.size(),
            sender_ed25519_pk.data()) != 0) {
        return std::nullopt;
    }

    Bytes nonce = from_b64(wrapped.nonce_b64);
    Bytes box = from_b64(wrapped.box_b64);
    if (nonce.size() != crypto_box_NONCEBYTES ||
        box.size() < crypto_box_MACBYTES) {
        return std::nullopt;
    }

    std::vector<unsigned char> inner(box.size() - crypto_box_MACBYTES);
    std::string inner_str;
    if (crypto_box_open_easy(inner.data(), box.data(), box.size(),
                             nonce.data(), sender_x25519_pk.data(),
                             own_x25519_sk.data()) != 0) {
        return std::nullopt;
    }
    inner_str.assign(inner.begin(), inner.end());

    auto j = nlohmann::json::parse(inner_str, nullptr, false);
    secure_wipe_str(inner_str);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }

    try {
        SenderKeyEnvelope env;
        env.room = j.at("room").get<std::string>();
        env.epoch = j.at("epoch").get<uint64_t>();
        env.key_id = j.at("key_id").get<std::string>();
        env.chain_key = from_b64(j.at("chain").get<std::string>());
        env.sender_x25519_b64 = j.at("x25519").get<std::string>();
        env.sender_ed25519_b64 = j.at("ed25519").get<std::string>();

        // Consistency: inner claims must match the (signed) outer context.
        if (env.room != room || env.epoch != expected_epoch ||
            env.key_id != expected_key_id ||
            env.chain_key.size() != ROOM_KEY_BYTES) {
            return std::nullopt;
        }

        // The identity keys inside the box must match the pinned keys we
        // verified the signature with, otherwise someone is mixing keys.
        if (env.sender_x25519_b64 != wrapped.sender_x25519_b64 ||
            env.sender_ed25519_b64 != wrapped.sender_ed25519_b64) {
            return std::nullopt;
        }

        return env;
    } catch (...) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Hygiene
// ---------------------------------------------------------------------------

void secure_wipe(Bytes& b) {
    if (!b.empty()) {
        sodium_memzero(b.data(), b.size());
    }
    b.clear();
}

void secure_wipe_str(std::string& s) {
    if (!s.empty()) {
        sodium_memzero(&s[0], s.size());
    }
    s.clear();
}

} // namespace chat::crypto
