#include "gistdb/binder/resolution.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "../test_utils/scoped_temp_file.hpp"
#include "gistdb/catalog/catalog.hpp"
#include "gistdb/catalog/column_def.hpp"
#include "gistdb/types.hpp"

namespace gistdb::binder {
namespace {

using gistdb::catalog::Catalog;
using gistdb::catalog::ColumnDef;
using gistdb::test_utils::ScopedTempFile;

std::filesystem::path FreshPath(const ScopedTempFile& temp) {
  std::filesystem::remove(temp.Path());
  return temp.Path();
}

Catalog MakeTestCatalog(const std::filesystem::path& path) {
  Catalog catalog = Catalog::CreateNew(path);
  catalog.CreateTable("users",
                      {ColumnDef{.name = "id", .type = gistdb::TypeId::kInteger, .ordinal = 0},
                       ColumnDef{.name = "name", .type = gistdb::TypeId::kVarchar, .ordinal = 1}});
  catalog.CreateTable(
      "orders", {ColumnDef{.name = "id", .type = gistdb::TypeId::kInteger, .ordinal = 0},
                 ColumnDef{.name = "user_id", .type = gistdb::TypeId::kInteger, .ordinal = 1}});
  return catalog;
}

TEST(ResolutionScopeTest, RegisterTableAssignsBindingIdsInCallOrder) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  std::uint32_t users_id = scope.RegisterTable("users", std::nullopt, catalog);
  std::uint32_t orders_id = scope.RegisterTable("orders", std::nullopt, catalog);

  EXPECT_EQ(users_id, 0U);
  EXPECT_EQ(orders_id, 1U);
}

TEST(ResolutionScopeTest, RegisterTableThrowsOnUnknownTable) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  EXPECT_THROW((void)scope.RegisterTable("ghosts", std::nullopt, catalog), ResolutionException);
}

TEST(ResolutionScopeTest, RegisterTableThrowsOnDuplicateUnaliasedSelfJoin) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::nullopt, catalog);
  EXPECT_THROW((void)scope.RegisterTable("users", std::nullopt, catalog), ResolutionException);
}

TEST(ResolutionScopeTest, SelfJoinWithDistinctAliasesSucceeds) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  std::uint32_t u1 = scope.RegisterTable("users", std::string("u1"), catalog);
  std::uint32_t u2 = scope.RegisterTable("users", std::string("u2"), catalog);

  EXPECT_NE(u1, u2);
  EXPECT_EQ(scope.BindingFor(u1).binding_name, "u1");
  EXPECT_EQ(scope.BindingFor(u2).binding_name, "u2");
}

TEST(ResolutionScopeTest, ResolvesQualifiedColumnAgainstBindingName) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  std::uint32_t binding_id = scope.RegisterTable("users", std::string("u"), catalog);

  ColumnRefNode ref{.table_qualifier = std::string("u"), .column_name = "name"};
  gistdb::execution::BoundColumnRef resolved = scope.Resolve(ref);

  EXPECT_EQ(resolved.table_id, binding_id);
  EXPECT_EQ(resolved.ordinal, 1U);
  EXPECT_EQ(resolved.type, gistdb::TypeId::kVarchar);
}

TEST(ResolutionScopeTest, QualifiedResolveThrowsOnUnknownAlias) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::nullopt, catalog);

  ColumnRefNode ref{.table_qualifier = std::string("ghost_alias"), .column_name = "name"};
  EXPECT_THROW((void)scope.Resolve(ref), ResolutionException);
}

TEST(ResolutionScopeTest, QualifiedResolveThrowsOnUnknownColumn) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::nullopt, catalog);

  ColumnRefNode ref{.table_qualifier = std::string("users"), .column_name = "does_not_exist"};
  EXPECT_THROW((void)scope.Resolve(ref), ResolutionException);
}

TEST(ResolutionScopeTest, UnqualifiedResolveFindsUniqueMatch) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::nullopt, catalog);
  scope.RegisterTable("orders", std::nullopt, catalog);

  ColumnRefNode ref{.table_qualifier = std::nullopt,
                    .column_name = "name"};  // only "users" has "name"
  gistdb::execution::BoundColumnRef resolved = scope.Resolve(ref);
  EXPECT_EQ(resolved.ordinal, 1U);
}

TEST(ResolutionScopeTest, UnqualifiedResolveThrowsOnAmbiguousColumn) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::string("u1"), catalog);
  scope.RegisterTable("users", std::string("u2"), catalog);  // both have "id"

  ColumnRefNode ref{.table_qualifier = std::nullopt, .column_name = "id"};
  EXPECT_THROW((void)scope.Resolve(ref), ResolutionException);
}

TEST(ResolutionScopeTest, UnqualifiedResolveThrowsWhenColumnNotFoundAnywhere) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  scope.RegisterTable("users", std::nullopt, catalog);

  ColumnRefNode ref{.table_qualifier = std::nullopt, .column_name = "does_not_exist"};
  EXPECT_THROW((void)scope.Resolve(ref), ResolutionException);
}

TEST(ResolutionScopeTest, BindingForReturnsRegisteredTableObject) {
  ScopedTempFile temp_file;
  Catalog catalog = MakeTestCatalog(FreshPath(temp_file));

  ResolutionScope scope;
  std::uint32_t binding_id = scope.RegisterTable("orders", std::nullopt, catalog);

  const ResolvedTableBinding& binding = scope.BindingFor(binding_id);
  EXPECT_EQ(binding.binding_name, "orders");
  EXPECT_EQ(binding.table->TableName(), "orders");
}

}  // namespace
}  // namespace gistdb::binder