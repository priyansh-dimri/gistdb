#include "gistdb/optimizer/optimizer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gistdb/binder/logical_plan.hpp"
#include "gistdb/execution/bound_expression.hpp"
#include "gistdb/types.hpp"

namespace gistdb::optimizer {
namespace {

using gistdb::binder::LogicalPlanNode;
using gistdb::binder::MakeLogicalAggregate;
using gistdb::binder::MakeLogicalFilter;
using gistdb::binder::MakeLogicalJoin;
using gistdb::binder::MakeLogicalProjection;
using gistdb::binder::MakeLogicalScan;
using gistdb::execution::BinaryOperator;
using gistdb::execution::BoundColumnRef;
using gistdb::execution::BoundExpression;
using gistdb::execution::ExpressionType;
using gistdb::execution::MakeBooleanOp;
using gistdb::execution::MakeColumnRef;
using gistdb::execution::MakeIntConst;

std::unique_ptr<BoundExpression> ColRef(std::uint32_t table_id, std::uint32_t ordinal) {
  return MakeColumnRef(table_id, ordinal, gistdb::TypeId::kInteger);
}

std::string OptimizeAndCaptureMessage(std::unique_ptr<LogicalPlanNode> root) {
  try {
    (void)Optimizer::Optimize(std::move(root));
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  ADD_FAILURE() << "Expected Optimizer::Optimize to throw, but it returned normally";
  return "";
}

TEST(OptimizerTest, BareScanThrowsSeqScanStubMessage) {
  auto scan = MakeLogicalScan(0, 100, {{"a", ExpressionType::kInteger}});
  std::string message = OptimizeAndCaptureMessage(std::move(scan));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
}

TEST(OptimizerTest, FilterOverScanRecursesIntoScanBeforeThrowing) {
  auto scan = MakeLogicalScan(0, 100, {{"a", ExpressionType::kInteger}});
  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(5));
  auto filter = MakeLogicalFilter(std::move(scan), std::move(predicate));

  std::string message = OptimizeAndCaptureMessage(std::move(filter));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
}

TEST(OptimizerTest, AggregateThrowsItsOwnStubMessageWithoutReachingScan) {
  auto scan = MakeLogicalScan(0, 100, {{"category", ExpressionType::kVarchar}});
  std::vector<BoundColumnRef> group_by = {
      BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kVarchar}};
  auto aggregate =
      MakeLogicalAggregate(std::move(scan), group_by, {}, {{"category", ExpressionType::kVarchar}});

  std::string message = OptimizeAndCaptureMessage(std::move(aggregate));
  EXPECT_NE(message.find("Aggregation operator"), std::string::npos);
  EXPECT_EQ(message.find("SeqScan"), std::string::npos);
}

TEST(OptimizerTest, ProjectionThrowsItsOwnStubMessageWithoutReachingScan) {
  auto scan = MakeLogicalScan(0, 100, {{"a", ExpressionType::kInteger}});
  std::vector<std::unique_ptr<BoundExpression>> select_exprs;
  select_exprs.push_back(ColRef(0, 0));
  auto projection = MakeLogicalProjection(std::move(scan), std::move(select_exprs),
                                          {{"a", ExpressionType::kInteger}});

  std::string message = OptimizeAndCaptureMessage(std::move(projection));
  EXPECT_NE(message.find("Projection operator"), std::string::npos);
  EXPECT_EQ(message.find("SeqScan"), std::string::npos);
}

TEST(OptimizerTest, JoinOfTwoBaseTablesComputesOrdinalsWithoutInternalErrorBeforeThrowing) {
  auto left_scan = MakeLogicalScan(
      0, 100, {{"id", ExpressionType::kInteger}, {"key", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(
      1, 200, {{"key", ExpressionType::kInteger}, {"value", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 1, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::string message = OptimizeAndCaptureMessage(std::move(join));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
  EXPECT_EQ(message.find("FindColumnPosition"), std::string::npos);
  EXPECT_EQ(message.find("PhysicalOutputSchema"), std::string::npos);
}

TEST(OptimizerTest, JoinWherePruningNarrowsBuildSideStillComputesTypesWithoutInternalError) {
  auto left_scan = MakeLogicalScan(0, 100, {{"id", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(1, 200,
                                    {{"unused_a", ExpressionType::kInteger},
                                     {"key", ExpressionType::kInteger},
                                     {"unused_b", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 1, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::string message = OptimizeAndCaptureMessage(std::move(join));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
  EXPECT_EQ(message.find("FindColumnPosition"), std::string::npos);
  EXPECT_EQ(message.find("PhysicalOutputSchema"), std::string::npos);
}

TEST(OptimizerTest, FilterAboveJoinPushesDownThenReachesScanStub) {
  auto left_scan = MakeLogicalScan(
      0, 100, {{"id", ExpressionType::kInteger}, {"key", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(1, 200, {{"key", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 1, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  auto predicate = MakeBooleanOp(BinaryOperator::kGreaterThan, ColRef(0, 0), MakeIntConst(5));
  auto filter = MakeLogicalFilter(std::move(join), std::move(predicate));

  std::string message = OptimizeAndCaptureMessage(std::move(filter));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
}

TEST(OptimizerTest, SelfJoinDistinguishesBindingIdsWithoutInternalError) {
  auto left_scan = MakeLogicalScan(0, 100, {{"id", ExpressionType::kInteger}});
  auto right_scan = MakeLogicalScan(1, 100, {{"id", ExpressionType::kInteger}});
  std::vector<std::pair<BoundColumnRef, BoundColumnRef>> equi = {
      {BoundColumnRef{.table_id = 0, .ordinal = 0, .type = gistdb::TypeId::kInteger},
       BoundColumnRef{.table_id = 1, .ordinal = 0, .type = gistdb::TypeId::kInteger}}};
  auto join = MakeLogicalJoin(std::move(left_scan), std::move(right_scan), std::move(equi));

  std::string message = OptimizeAndCaptureMessage(std::move(join));
  EXPECT_NE(message.find("SeqScan"), std::string::npos);
}

}  // namespace
}  // namespace gistdb::optimizer