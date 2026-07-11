#include "gistdb/catalog/schema_section.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace gistdb::catalog {
namespace {

TEST(SchemaSectionTest, StartsEmpty) {
  SchemaSection schema;
  EXPECT_EQ(schema.NextTableId(), 0U);
  EXPECT_EQ(schema.NumTables(), 0U);
}

TEST(SchemaSectionTest, AddTableAssignsSequentialIdsStartingAtZero) {
  SchemaSection schema;
  std::uint32_t id1 = schema.AddTable("users", {ColumnDef{
                                                   .name = "id",
                                                   .type = gistdb::TypeId::kInteger,
                                                   .ordinal = 0,
                                               }});

  std::uint32_t id2 = schema.AddTable("orders", {ColumnDef{
                                                    .name = "id",
                                                    .type = gistdb::TypeId::kInteger,
                                                    .ordinal = 0,
                                                }});
  EXPECT_EQ(id1, 0U);
  EXPECT_EQ(id2, 1U);
  EXPECT_EQ(schema.NextTableId(), 2U);
}

TEST(SchemaSectionTest, TableAccessReturnsStoredFields) {
  SchemaSection schema;
  std::vector<ColumnDef> columns = {
      ColumnDef{
          .name = "id",
          .type = gistdb::TypeId::kInteger,
          .ordinal = 0,
      },
      ColumnDef{
          .name = "name",
          .type = gistdb::TypeId::kVarchar,
          .ordinal = 1,
      },
  };
  schema.AddTable("users", columns);

  ASSERT_EQ(schema.NumTables(), 1U);
  const TableSchemaEntry& entry = schema.Table(0);
  EXPECT_EQ(entry.table_id, 0U);
  EXPECT_EQ(entry.table_name, "users");
  EXPECT_EQ(entry.columns, columns);
}

TEST(SchemaSectionTest, EmptySchemaRoundTrips) {
  SchemaSection schema;
  SchemaSection restored = SchemaSection::Deserialize(schema.Serialize());
  EXPECT_EQ(restored.NextTableId(), 0U);
  EXPECT_EQ(restored.NumTables(), 0U);
}

TEST(SchemaSectionTest, PopulatedSchemaRoundTripsFully) {
  SchemaSection schema;
  schema.AddTable("users", {ColumnDef{
                                .name = "id",
                                .type = gistdb::TypeId::kInteger,
                                .ordinal = 0,
                            },
                            ColumnDef{
                                .name = "name",
                                .type = gistdb::TypeId::kVarchar,
                                .ordinal = 1,
                            }});

  schema.AddTable("orders", {ColumnDef{
                                .name = "total",
                                .type = gistdb::TypeId::kFloat,
                                .ordinal = 0,
                            }});
  SchemaSection restored = SchemaSection::Deserialize(schema.Serialize());

  EXPECT_EQ(restored.NextTableId(), 2U);
  ASSERT_EQ(restored.NumTables(), 2U);

  EXPECT_EQ(restored.Table(0).table_id, 0U);
  EXPECT_EQ(restored.Table(0).table_name, "users");
  ASSERT_EQ(restored.Table(0).columns.size(), 2U);
  EXPECT_EQ(restored.Table(0).columns[0].name, "id");
  EXPECT_EQ(restored.Table(0).columns[1].type, gistdb::TypeId::kVarchar);

  EXPECT_EQ(restored.Table(1).table_name, "orders");
  EXPECT_EQ(restored.Table(1).columns[0].name, "total");
}

TEST(SchemaSectionTest, TableWithNoColumnsRoundTrips) {
  SchemaSection schema;
  schema.AddTable("empty_table", {});
  SchemaSection restored = SchemaSection::Deserialize(schema.Serialize());
  EXPECT_EQ(restored.Table(0).columns.size(), 0U);
}

}  // namespace
}  // namespace gistdb::catalog