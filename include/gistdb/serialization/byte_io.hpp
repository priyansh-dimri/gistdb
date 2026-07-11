#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gistdb::serialization {

void WriteU8(std::vector<std::uint8_t>& buf, std::uint8_t value);
void WriteU32(std::vector<std::uint8_t>& buf, std::uint32_t value);
void WriteU64(std::vector<std::uint8_t>& buf, std::uint64_t value);
void WriteFloat(std::vector<std::uint8_t>& buf, float value);
void WriteString(std::vector<std::uint8_t>& buf, const std::string& value);  // length-prefixed

// Sequentially reads primitives out of an already-fully-received byte
// buffer. Does no bounds checking -- assumes well-formed input,
// consistent with this project's accepted no-crash-consistency-
// guarantee scope (Storage Checkpoint, Decision 8.7).
class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  std::uint8_t ReadU8();
  std::uint32_t ReadU32();
  std::uint64_t ReadU64();
  float ReadFloat();
  std::string ReadString();                        // length-prefixed
  std::string ReadFixedBytes(std::size_t length);  // exactly `length` raw bytes, no length prefix
  void Skip(std::size_t count) { pos_ += count; }

 private:
  const std::vector<std::uint8_t>& bytes_;  // NOLINT
  std::size_t pos_ = 0;
};

}  // namespace gistdb::serialization