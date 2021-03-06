//
// Created by Iscle on 26/01/2021.
//

#include "session.h"
#include "ap_resolver.h"
#include "../utils/byte_array.h"
#include "../utils/byte_buffer.h"
#include "../crypto/hmac_sha_1.h"
#include "../time_provider.h"
#include <vector>
#include <cstdint>
#include <iostream>
#include <memory>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <spdlog/spdlog.h>
#include <netdb.h>

static constexpr uint8_t SERVER_KEY[] = {
        0xac, 0xe0, 0x46, 0x0b, 0xff, 0xc2, 0x30, 0xaf, 0xf4, 0x6b, 0xfe, 0xc3, 0xbf, 0xbf, 0x86, 0x3d, 0xa1, 0x91,
        0xc6, 0xcc, 0x33, 0x6c, 0x93, 0xa1, 0x4f, 0xb3, 0xb0, 0x16, 0x12, 0xac, 0xac, 0x6a, 0xf1, 0x80, 0xe7, 0xf6,
        0x14, 0xd9, 0x42, 0x9d, 0xbe, 0x2e, 0x34, 0x66, 0x43, 0xe3, 0x62, 0xd2, 0x32, 0x7a, 0x1a, 0x0d, 0x92, 0x3b,
        0xae, 0xdd, 0x14, 0x02, 0xb1, 0x81, 0x55, 0x05, 0x61, 0x04, 0xd5, 0x2c, 0x96, 0xa4, 0x4c, 0x1e, 0xcc, 0x02,
        0x4a, 0xd4, 0xb2, 0x0c, 0x00, 0x1f, 0x17, 0xed, 0xc2, 0x2f, 0xc4, 0x35, 0x21, 0xc8, 0xf0, 0xcb, 0xae, 0xd2,
        0xad, 0xd7, 0x2b, 0x0f, 0x9d, 0xb3, 0xc5, 0x32, 0x1a, 0x2a, 0xfe, 0x59, 0xf3, 0x5a, 0x0d, 0xac, 0x68, 0xf1,
        0xfa, 0x62, 0x1e, 0xfb, 0x2c, 0x8d, 0x0c, 0xb7, 0x39, 0x2d, 0x92, 0x47, 0xe3, 0xd7, 0x35, 0x1a, 0x6d, 0xbd,
        0x24, 0xc2, 0xae, 0x25, 0x5b, 0x88, 0xff, 0xab, 0x73, 0x29, 0x8a, 0x0b, 0xcc, 0xcd, 0x0c, 0x58, 0x67, 0x31,
        0x89, 0xe8, 0xbd, 0x34, 0x80, 0x78, 0x4a, 0x5f, 0xc9, 0x6b, 0x89, 0x9d, 0x95, 0x6b, 0xfc, 0x86, 0xd7, 0x4f,
        0x33, 0xa6, 0x78, 0x17, 0x96, 0xc9, 0xc3, 0x2d, 0x0d, 0x32, 0xa5, 0xab, 0xcd, 0x05, 0x27, 0xe2, 0xf7, 0x10,
        0xa3, 0x96, 0x13, 0xc4, 0x2f, 0x99, 0xc0, 0x27, 0xbf, 0xed, 0x04, 0x9c, 0x3c, 0x27, 0x58, 0x04, 0xb6, 0xb2,
        0x19, 0xf9, 0xc1, 0x2f, 0x02, 0xe9, 0x48, 0x63, 0xec, 0xa1, 0xb6, 0x42, 0xa0, 0x9d, 0x48, 0x25, 0xf8, 0xb3,
        0x9d, 0xd0, 0xe8, 0x6a, 0xf9, 0x48, 0x4d, 0xa1, 0xc2, 0xba, 0x86, 0x30, 0x42, 0xea, 0x9d, 0xb3, 0x08, 0x6c,
        0x19, 0x0e, 0x48, 0xb3, 0x9d, 0x66, 0xeb, 0x00, 0x06, 0xa2, 0x5a, 0xee, 0xa1, 0x1b, 0x13, 0x87, 0x3c, 0xd7,
        0x19, 0xe6, 0x55, 0xbd
};

Session::Session(const std::string& accesspoint) : conn(Connection(accesspoint)),
        // Ping is every two minutes. Give a 5 second margin before triggering a reconnection.
        scheduled_reconnect(DelayedTask(std::chrono::seconds(120 + 5), [this] {
            SPDLOG_WARN("Socket timed out. Reconnecting...");
            reconnect();
        })) {
    this->running = false;
    this->auth_lock = false;

    SPDLOG_INFO("Created new session! deviceId: {}", generate_device_id(), false);
}

void Session::connect() {
    ByteArray acc;
    DiffieHellman dh;

    send_client_hello(acc, dh);

    // Read ClientHello response
    int length = conn.read_int();
    auto buffer = conn.read_fully(length - 4);
    acc.write_int(htonl(length));
    acc.write(buffer);

    // Read APResponseMessage
    spotify::APResponseMessage ap_response_message;
    ap_response_message.ParseFromArray(buffer.data(), buffer.size());

    check_gs_signature(ap_response_message);

    ByteArray challenge_data;
    auto challenge = solve_challenge(acc, dh, ap_response_message, challenge_data);
    send_challenge_response(challenge);

    read_connection_status();

    // TODO: synchronize auth_lock
    cipher_pair = std::make_unique<CipherPair>(conn, &challenge_data[20], 32, &challenge_data[52], 32);
    auth_lock = true;
    // TODO: end synchronize auth_lock

    SPDLOG_INFO("Connected successfully!");
}

void Session::reconnect() {
    SPDLOG_ERROR("Oopsie doopsie, reconnect hasn't been implemented yet!");
}

void Session::send_client_hello(ByteArray &acc, DiffieHellman &dh) {
    spotify::ClientHello client_hello;
    uint8_t nonce[16];
    uint8_t padding[] = {0x1E};

    // Send ClientHello
    RAND_bytes(nonce, sizeof(nonce));

    Version::build_info(client_hello.mutable_build_info());
    client_hello.add_cryptosuites_supported(spotify::CRYPTO_SUITE_SHANNON);
    client_hello.mutable_login_crypto_hello()->mutable_diffie_hellman()->set_gc(dh.get_public_key(),
                                                                                dh.get_public_key_length());
    client_hello.mutable_login_crypto_hello()->mutable_diffie_hellman()->set_server_keys_known(1);
    client_hello.set_client_nonce(nonce, sizeof(nonce));
    client_hello.set_padding(padding, sizeof(padding));

    auto client_hello_string = client_hello.SerializeAsString();
    unsigned int length = 1 + 1 + 4 + (unsigned int) client_hello_string.size();
    conn.write(0);
    conn.write(4);
    conn.write_int(length);
    conn.write(client_hello_string);

    acc.write(0);
    acc.write(4);
    acc.write_int(htonl(length));
    acc.write(client_hello_string);
}

void Session::check_gs_signature(spotify::APResponseMessage &response) {
    RSA *rsa = RSA_new();
    BIGNUM *n = BN_bin2bn(SERVER_KEY, sizeof(SERVER_KEY), nullptr);
    BIGNUM *e = nullptr;
    BN_dec2bn(&e, "65537");
    RSA_set0_key(rsa, n, e, nullptr);
    EVP_PKEY *pub_key = EVP_PKEY_new();
    EVP_PKEY_set1_RSA(pub_key, rsa);
    EVP_MD_CTX *rsa_verify_ctx = EVP_MD_CTX_new();

    if (EVP_DigestVerifyInit(rsa_verify_ctx, nullptr, EVP_sha1(), nullptr, pub_key) != 1) {
        // TODO: Handle error
        SPDLOG_ERROR("Failed to init digest verify!");
    }

    auto gs = response.challenge().login_crypto_challenge().diffie_hellman().gs();
    if (EVP_DigestVerifyUpdate(rsa_verify_ctx, gs.c_str(), gs.size()) != 1) {
        // TODO: Handle error
        SPDLOG_ERROR("Failed to update digest verify!");
    }

    auto gs_signature = response.challenge().login_crypto_challenge().diffie_hellman().gs_signature();
    if (EVP_DigestVerifyFinal(rsa_verify_ctx, (const unsigned char *) gs_signature.c_str(), gs_signature.size()) != 1) {
        // TODO: Handle error
        SPDLOG_ERROR("Failed to verify digest!");
    }

    EVP_MD_CTX_free(rsa_verify_ctx);
    EVP_PKEY_free(pub_key);
    RSA_free(rsa);
}

std::vector<uint8_t>
Session::solve_challenge(ByteArray &acc, DiffieHellman &dh, spotify::APResponseMessage &response,
                         ByteArray &data) {
    auto shared_key = dh.compute_shared_key(response.challenge().login_crypto_challenge().diffie_hellman().gs());
    HMAC_SHA1 hmac;
    std::vector<uint8_t> tmp;

    for (uint8_t i = 1; i < 6; i++) {
        hmac.init(shared_key);
        hmac.update(acc);
        hmac.update(&i, 1);
        hmac.final(tmp);
        data.write(reinterpret_cast<const char *>(tmp.data()), tmp.size());
    }

    std::vector<uint8_t> hmac_data(data.begin(), data.begin() + 20);
    hmac.init(hmac_data);
    hmac.update(acc);
    hmac.final(tmp);
    return tmp;
}

void Session::send_challenge_response(std::vector<uint8_t> &challenge) {
    spotify::ClientResponsePlaintext client_response_plaintext;
    client_response_plaintext.mutable_login_crypto_response()->mutable_diffie_hellman()->set_hmac(challenge.data(),
                                                                                                  challenge.size());
    client_response_plaintext.mutable_pow_response();
    client_response_plaintext.mutable_crypto_response();

    auto client_response_plaintext_string = client_response_plaintext.SerializeAsString();
    unsigned int length = 4 + (unsigned int) client_response_plaintext_string.size();
    conn.write_int(length);
    conn.write(client_response_plaintext_string);
}

void Session::read_connection_status() {
    conn.set_timeout(1);
    auto scrap = conn.read(4);
    conn.restore_timeout();
    if (scrap.size() == 4) {
        // TODO: Handle error
        unsigned int length = (scrap[0] << 24) | (scrap[1] << 16) | (scrap[2] << 8) | (scrap[3] << 0);
        auto payload = conn.read_fully(length - 4);
        spotify::APResponseMessage ap_error_message;
        ap_error_message.ParseFromArray(payload.data(), payload.size());
        SPDLOG_ERROR("Connection failed! Error code: {}!", ap_error_message.login_failed().error_code());
        throw std::runtime_error(
                "Connection failed! Error code: " + std::to_string(ap_error_message.login_failed().error_code()));
    } else if (!scrap.empty()) {
        // TODO: Handle error
        SPDLOG_ERROR("Read unknown data!");
        throw std::runtime_error("Read unknown data!");
    }
}

void Session::packet_receiver() {
    SPDLOG_DEBUG("Session packet receiver started!");

    while (running) {
        Packet packet = cipher_pair->receive_encoded();

        if (!running) break;

        switch (packet.cmd) {
            case Packet::Type::Ping: {
                SPDLOG_DEBUG("Handling Ping!");
                scheduled_reconnect.reset();

                // Get and update timestamp
                ByteBuffer bb(packet.payload);
                TimeProvider::get_instance().update(ntohl(bb.get_int()) * 1000UL);

                send(Packet::Type::Pong, packet.payload);
                break;
            }
            case Packet::Type::PongAck: {
                SPDLOG_DEBUG("Handling PongAck!");
                // Silent
                break;
            }
            case Packet::Type::CountryCode: {
                SPDLOG_DEBUG("Handling CountryCode!");
                country_code = std::string(packet.payload.begin(), packet.payload.end());
                SPDLOG_DEBUG("Received CountryCode: {}", country_code);
                break;
            }
            case Packet::Type::LicenseVersion: {
                SPDLOG_DEBUG("Handling LicenseVersion!");
                // Unused
                break;
            }
            case Packet::Type::Unknown_0x10: {
                SPDLOG_DEBUG("Handling Unknown_0x10");
                // Unused
                break;
            }
            case Packet::Type::MercurySub:
            case Packet::Type::MercuryUnsub:
            case Packet::Type::MercuryEvent:
            case Packet::Type::MercuryReq: {
                SPDLOG_DEBUG("Handling Mercury packet!");
                mercury()->dispatch(packet);
                break;
            }
            case Packet::Type::AesKey:
            case Packet::Type::AesKeyError: {
                SPDLOG_DEBUG("Handling Aes packet!");
                //audio_key()->dispatch(packet);
                break;
            }
            case Packet::Type::ChannelError:
            case Packet::Type::StreamChunkRes: {
                SPDLOG_DEBUG("Handling Channel packet!");
                //channel()->dispatch(packet);
                break;
            }
            case Packet::Type::ProductInfo: {
                SPDLOG_DEBUG("Handling ProductInfo!");
                SPDLOG_DEBUG("{}", std::string(packet.payload.begin(), packet.payload.end()));
                break;
            }
            default: {
                SPDLOG_DEBUG("Skipping 0x{0:x}", packet.cmd);
                break;
            }
        }
    }

    SPDLOG_DEBUG("Session packet receiver ended!");
}

void Session::authenticate(spotify::LoginCredentials &credentials) {
    if (cipher_pair == nullptr) {
        // TODO: Handle error
        SPDLOG_ERROR("Connection not established!");
    }

    spotify::ClientResponseEncrypted client_response_encrypted;
    client_response_encrypted.set_allocated_login_credentials(&credentials);
    Version::system_info(client_response_encrypted.mutable_system_info());
    client_response_encrypted.set_version_string(Version::version_string());

    auto client_response_string = client_response_encrypted.SerializeAsString();
    client_response_encrypted.release_login_credentials(); // We're not the owner of LoginCredentials
    send_unchecked(Packet::Type::Login, client_response_string);

    Packet packet = cipher_pair->receive_encoded();
    if (packet.cmd == Packet::Type::APWelcome) {
        ap_welcome.ParseFromArray(packet.payload.data(), packet.payload.size());
        SPDLOG_INFO("Authenticated as {}!", ap_welcome.canonical_username());
    } else if (packet.cmd == Packet::Type::AuthFailure) {
        // TODO: Handle error
        spotify::APLoginFailed login_failed;
        login_failed.ParseFromArray(packet.payload.data(), packet.payload.size());
        SPDLOG_ERROR("Login failed! Error code: {}", login_failed.error_code());
        throw std::runtime_error("Login failed! Error code: " + std::to_string(login_failed.error_code()));
    } else {
        SPDLOG_WARN("Unknown command received! cmd: {0:x}", packet.cmd);
    }
}

void Session::start() {

    /*std::vector<uint8_t> bytes0x0f(20);
    RAND_bytes(bytes0x0f.data(), bytes0x0f.size());
    send_unchecked(Packet::Type::Unknown_0x0f, bytes0x0f);

    ByteArray preferred_locale;
    preferred_locale.write_byte(0x00);
    preferred_locale.write_byte(0x00);
    preferred_locale.write_byte(0x10);
    preferred_locale.write(0x00);
    preferred_locale.write(0x02);
    preferred_locale.write("preferred-locale");
    preferred_locale.write("en"); // TODO: Fix this with some sort of config :)
    send_unchecked(Packet::Type::PreferredLocale, preferred_locale);
    */

    running = true;
    mercury_client = std::make_unique<MercuryClient>();
    receiver = std::thread(&Session::packet_receiver, this);
    //mercury_client->interested_in("spotify:user:attributes:update", nullptr);
}

void Session::send_unchecked(Packet::Type cmd, std::vector<uint8_t> &payload) {
    cipher_pair->send_encoded(cmd, payload);
}

void Session::send_unchecked(Packet::Type cmd, std::string &payload) {
    auto vector = std::vector<uint8_t>(payload.begin(), payload.end());
    cipher_pair->send_encoded(cmd, vector);
}

void Session::send(Packet::Type cmd, std::vector<uint8_t> &payload) {
    if (false/*closing || conn == nullptr*/) {
        SPDLOG_DEBUG("Connection was broken while closing.");
        return;
    }

    //if (closed) throw std::runtime_error("Session is closed!");

    //synchronized (auth_lock) {
    if (cipher_pair == nullptr || auth_lock) {
        //auth_lock.wait();
    }

    send_unchecked(cmd, payload);
    //}
}

const std::unique_ptr<MercuryClient> &Session::mercury() const {
    // waitAuthLock();
    if (mercury_client == nullptr) {
        // TODO: Handle error
        SPDLOG_ERROR("Session isn't authenticated");
    }
    return mercury_client;
}

const std::unique_ptr<AudioKeyManager> &Session::audio_key() const {
    // waitAuthLock();
    if (audio_key_manager == nullptr) {
        // TODO: Handle error
        SPDLOG_ERROR("Session isn't authenticated");
    }
    return audio_key_manager;
}

const std::unique_ptr<ChannelManager> &Session::channel() const {
    // waitAuthLock();
    if (channel_manager == nullptr) {
        // TODO: Handle error
        SPDLOG_ERROR("Session isn't authenticated");
    }
    return channel_manager;
}

Session::~Session() {
    running = false;
    receiver.join();
}
