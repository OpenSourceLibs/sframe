
#include <doctest/doctest.h>
#include <sframe/sframe.h>

#include <iostream>  // for string, operator<<
#include <map>       // for map
#include <stdexcept> // for invalid_argument
#include <string>    // for basic_string, operator==

using namespace sframe;

bytes
from_hex(const std::string& hex)
{
  if (hex.length() % 2 == 1) {
    throw std::invalid_argument("Odd-length hex string");
  }

  auto len = int(hex.length() / 2);
  auto out = bytes(len);
  for (int i = 0; i < len; i += 1) {
    auto byte = hex.substr(2 * i, 2);
    out[i] = static_cast<uint8_t>(strtol(byte.c_str(), nullptr, 16));
  }

  return out;
}

std::string
ciphersuite_name(CipherSuite suite)
{
  switch (suite) {
    case CipherSuite::AES_GCM_128_SHA256:
      return "AES_GCM_128";

    case CipherSuite::AES_GCM_256_SHA512:
      return "AES_GCM_256";

    default:
      return "Unknown ciphersuite";
  }
}

template<typename T>
bytes
to_bytes(const T& range)
{
  return bytes(range.begin(), range.end());
}

TEST_CASE("SFrame Known-Answer")
{
  struct KnownAnswerTest
  {
    bytes key;
    bytes short_kid_ctr0;
    bytes short_kid_ctr1;
    bytes short_kid_ctr2;
    bytes long_kid_short_ctr;
    bytes long_kid_long_ctr;
  };

  const auto short_kid = KeyID(0x07);
  const auto long_kid = KeyID(0xffff);
  const auto long_ctr = KeyID(0x0100);
  const auto plaintext = from_hex("00010203");
  const std::map<CipherSuite, KnownAnswerTest> cases{
    { CipherSuite::AES_CM_128_HMAC_SHA256_4,
      {
        from_hex("101112131415161718191a1b1c1d1e1f"),
        from_hex("170023b51101e8cf3180"),
        from_hex("1701aa0743f6fed8c056"),
        from_hex("1702eae8243335f26dc9"),
        from_hex("1affff0023b51101b0927605"),
        from_hex("2affff01001981bb4f5d35ad0c"),
      } },
    { CipherSuite::AES_CM_128_HMAC_SHA256_8,
      {
        from_hex("202122232425262728292a2b2c2d2e2f"),
        from_hex("170022067e9270080090597dfadc"),
        from_hex("1701d868b21f5e80434093d12eef"),
        from_hex("170266de5b9332a80dea44a6407c"),
        from_hex("1affff0022067e92500ce44901a10eef"),
        from_hex("2affff01005ba58d1302a41630f1214e17"),
      } },
    { CipherSuite::AES_GCM_128_SHA256,
      {
        from_hex("303132333435363738393a3b3c3d3e3f"),
        from_hex("170048310f3b8c8a7297a92b3ed392938f9d0d087118"),
        from_hex("170145c8c2cd5ef5773e38f23ee6236a623f8351cfce"),
        from_hex("17021ea6e7b05246606050b44fe105f419dea85b4b7a"),
        from_hex("1affff0048310f3b542c2bc859816a10ee5f83f4f840f6e5"),
        from_hex("2affff0100f1f838df14b1e675fb0b0618291838e628fea346"),
      } },
    { CipherSuite::AES_GCM_256_SHA512,
      {
        from_hex(
          "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"),
        from_hex("1700b591faafe60c9c3a7d8dd1c18f91a72c510c8e63"),
        from_hex("1701d555e665358a2486d99ac7272bedd503f53ec9d7"),
        from_hex("170222e5fcd4709da8cc4d4a4e6e38a0b16afd0063fc"),
        from_hex("1affff00b591faafc843b5831c7fc08b477d926f8c4c8f9b"),
        from_hex("2affff01007b0e9ee905ab26c73927d7ece036a08c618610e4"),
      } },
  };

  auto pt_out = bytes(plaintext.size());
  auto ct_out = bytes(plaintext.size() + max_overhead);

  for (auto& pair : cases) {
    auto& suite = pair.first;
    auto& tc = pair.second;

    auto ctx = Context(suite);
    ctx.add_key(short_kid, tc.key);
    ctx.add_key(long_kid, tc.key);

    // KID=0x07, CTR=0, 1, 2
    auto ct0 = to_bytes(ctx.protect(short_kid, ct_out, plaintext));
    auto ct1 = to_bytes(ctx.protect(short_kid, ct_out, plaintext));
    auto ct2 = to_bytes(ctx.protect(short_kid, ct_out, plaintext));

    CHECK(ct0 == tc.short_kid_ctr0);
    CHECK(ct1 == tc.short_kid_ctr1);
    CHECK(ct2 == tc.short_kid_ctr2);

    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct0)));
    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct1)));
    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct2)));

    // KID=0xffff, CTR=0
    auto ctLS = to_bytes(ctx.protect(long_kid, ct_out, plaintext));
    for (Counter ctr = 1; ctr < long_ctr; ctr++) {
      ctx.protect(long_kid, ct_out, plaintext);
    }
    auto ctLL = to_bytes(ctx.protect(long_kid, ct_out, plaintext));

    CHECK(to_bytes(ctLS) == tc.long_kid_short_ctr);
    CHECK(to_bytes(ctLL) == tc.long_kid_long_ctr);

    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct0)));
    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct1)));
    CHECK(plaintext == to_bytes(ctx.unprotect(pt_out, ct2)));
  }
}

TEST_CASE("SFrame Round-Trip")
{
  const auto rounds = 1 << 9;
  const auto kid = KeyID(0x42);
  const auto plaintext = from_hex("00010203");
  const std::map<CipherSuite, bytes> keys{
    { CipherSuite::AES_CM_128_HMAC_SHA256_4,
      from_hex("101112131415161718191a1b1c1d1e1f") },
    { CipherSuite::AES_CM_128_HMAC_SHA256_8,
      from_hex("202122232425262728292a2b2c2d2e2f") },
    { CipherSuite::AES_GCM_128_SHA256,
      from_hex("303132333435363738393a3b3c3d3e3f") },
    { CipherSuite::AES_GCM_256_SHA512,
      from_hex("404142434445464748494a4b4c4d4e4f"
               "505152535455565758595a5b5c5d5e5f") },
  };

  auto pt_out = bytes(plaintext.size());
  auto ct_out = bytes(plaintext.size() + max_overhead);

  for (auto& pair : keys) {
    auto& suite = pair.first;
    auto& key = pair.second;

    auto send = Context(suite);
    send.add_key(kid, key);

    auto recv = Context(suite);
    recv.add_key(kid, key);

    for (int i = 0; i < rounds; i++) {
      auto encrypted = to_bytes(send.protect(kid, ct_out, plaintext));
      auto decrypted = to_bytes(recv.unprotect(pt_out, encrypted));
      CHECK(decrypted == plaintext);
    }
  }
}

TEST_CASE("MLS Known-Answer") {
  // TODO
}

TEST_CASE("MLS Round-Trip") {
  const auto epoch_bits = 2;
  const auto test_epochs = 1 << (epoch_bits + 1);
  const auto epoch_rounds = 10;
  const auto plaintext = from_hex("00010203");
  const auto sender_id_a = MLSContext::SenderID(0xA0A0A0A0);
  const auto sender_id_b = MLSContext::SenderID(0xA1A1A1A1);
  const std::vector<CipherSuite> suites{
    CipherSuite::AES_CM_128_HMAC_SHA256_4,
    CipherSuite::AES_CM_128_HMAC_SHA256_8,
    CipherSuite::AES_GCM_128_SHA256,
    CipherSuite::AES_GCM_256_SHA512,
  };

  auto pt_out = bytes(plaintext.size());
  auto ct_out = bytes(plaintext.size() + max_overhead);

  for (auto& suite : suites) {
    auto member_a = MLSContext(suite, epoch_bits);
    auto member_b = MLSContext(suite, epoch_bits);

    for (MLSContext::EpochID epoch_id = 0; epoch_id < test_epochs; epoch_id++) {
      const auto sframe_epoch_secret = bytes(8, uint8_t(epoch_id));

      member_a.add_epoch(epoch_id, sframe_epoch_secret);
      member_b.add_epoch(epoch_id, sframe_epoch_secret);

      for (int i = 0; i < epoch_rounds; i++) {
        auto encrypted_ab = member_a.protect(epoch_id, sender_id_a, ct_out, plaintext);
        auto decrypted_ab = member_b.unprotect(pt_out, encrypted_ab);
        CHECK(plaintext == to_bytes(decrypted_ab));

        auto encrypted_ba = member_b.protect(epoch_id, sender_id_b, ct_out, plaintext);
        auto decrypted_ba = member_a.unprotect(pt_out, encrypted_ba);
        CHECK(plaintext == to_bytes(decrypted_ba));
      }
    }
  }
}
