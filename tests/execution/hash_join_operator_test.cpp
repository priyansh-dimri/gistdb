#include "gistdb/execution/hash_join_operator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../test_utils/mock_operator.hpp"
#include "gistdb/constants.hpp"
#include "gistdb/execution/data_chunk.hpp"
#include "gistdb/storage/fixed_width_column.hpp"

namespace gistdb::execution {
namespace {

using gistdb::test_utils::MockOperator;

TEST(HashJoinOperatorTest, BasicEquiJoinMatchesOnKey) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.Append(1);
  build_key.Append(2);
  build_key.Append(3);
  gistdb::storage::FixedWidthColumn<std::int32_t> build_value;
  build_value.Append(100);
  build_value.Append(200);
  build_value.Append(300);
  DataChunk build_chunk(3);
  build_chunk.AddColumn(&build_key);
  build_chunk.AddColumn(&build_value);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(2);
  probe_key.Append(4);
  DataChunk probe_chunk(2);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger, gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 1U);

  const auto* out_build_key =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(result->Column(0));
  const auto* out_build_value =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(result->Column(1));
  const auto* out_probe_key =
      std::get<const gistdb::storage::FixedWidthColumn<std::int32_t>*>(result->Column(2));

  EXPECT_EQ(out_build_key->GetValue(0), 2);
  EXPECT_EQ(out_build_value->GetValue(0), 200);
  EXPECT_EQ(out_probe_key->GetValue(0), 2);
}

TEST(HashJoinOperatorTest, NullBuildKeyNeverMatches) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.AppendNull();
  build_key.Append(5);
  DataChunk build_chunk(2);
  build_chunk.AddColumn(&build_key);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(5);
  DataChunk probe_chunk(1);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 1U);
}

TEST(HashJoinOperatorTest, NullProbeKeyNeverMatches) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.Append(5);
  DataChunk build_chunk(1);
  build_chunk.AddColumn(&build_key);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.AppendNull();
  probe_key.Append(5);
  DataChunk probe_chunk(2);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 1U);
}

TEST(HashJoinOperatorTest, FanOutOneProbeRowMatchesMultipleBuildRows) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.Append(1);
  build_key.Append(1);
  build_key.Append(1);
  DataChunk build_chunk(3);
  build_chunk.AddColumn(&build_key);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(1);
  DataChunk probe_chunk(1);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 3U);
}

TEST(HashJoinOperatorTest, UnselectedRowsAreExcludedFromBuildAndProbe) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.Append(1);
  build_key.Append(2);
  DataChunk build_chunk(2);
  build_chunk.AddColumn(&build_key);
  build_chunk.SetRowSelected(1, false);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(1);
  probe_key.Append(2);
  DataChunk probe_chunk(2);
  probe_chunk.AddColumn(&probe_key);
  probe_chunk.SetRowSelected(0, false);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 0U);
}

TEST(HashJoinOperatorTest, BuildSpansMultipleChunksBeforeAnyProbing) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key1;
  build_key1.Append(1);
  DataChunk build_chunk1(1);
  build_chunk1.AddColumn(&build_key1);

  gistdb::storage::FixedWidthColumn<std::int32_t> build_key2;
  build_key2.Append(2);
  DataChunk build_chunk2(1);
  build_chunk2.AddColumn(&build_key2);

  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk1));
  build_chunks.push_back(std::move(build_chunk2));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(1);
  probe_key.Append(2);
  DataChunk probe_chunk(2);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> result = join.GetNext();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->RowCount(), 2U);
}

TEST(HashJoinOperatorTest, ChunkedEmissionAcrossMultipleGetNextCalls) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key1;
  for (std::uint32_t i = 0; i < gistdb::kVectorSize; ++i) {
    build_key1.Append(1);
  }
  DataChunk build_chunk1(gistdb::kVectorSize);
  build_chunk1.AddColumn(&build_key1);

  gistdb::storage::FixedWidthColumn<std::int32_t> build_key2;
  for (std::uint32_t i = 0; i < 5; ++i) {
    build_key2.Append(1);
  }
  DataChunk build_chunk2(5);
  build_chunk2.AddColumn(&build_key2);

  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk1));
  build_chunks.push_back(std::move(build_chunk2));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  gistdb::storage::FixedWidthColumn<std::int32_t> probe_key;
  probe_key.Append(1);
  DataChunk probe_chunk(1);
  probe_chunk.AddColumn(&probe_key);
  std::vector<DataChunk> probe_chunks;
  probe_chunks.push_back(std::move(probe_chunk));
  auto probe_child = std::make_unique<MockOperator>(std::move(probe_chunks));

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  std::optional<DataChunk> first = join.GetNext();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->RowCount(), gistdb::kVectorSize);

  std::optional<DataChunk> second = join.GetNext();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->RowCount(), 5U);

  EXPECT_FALSE(join.GetNext().has_value());
}

TEST(HashJoinOperatorTest, EmptyProbeSideProducesNoRows) {
  gistdb::storage::FixedWidthColumn<std::int32_t> build_key;
  build_key.Append(1);
  DataChunk build_chunk(1);
  build_chunk.AddColumn(&build_key);
  std::vector<DataChunk> build_chunks;
  build_chunks.push_back(std::move(build_chunk));
  auto build_child = std::make_unique<MockOperator>(std::move(build_chunks));

  auto probe_child = std::make_unique<MockOperator>(std::vector<DataChunk>{});

  HashJoinOperator join(std::move(build_child), std::move(probe_child), {0}, {0},
                        {gistdb::TypeId::kInteger});

  EXPECT_FALSE(join.GetNext().has_value());
}

}  // namespace
}  // namespace gistdb::execution