import random

ROW_GROUP_SIZE = 10240
NUM_USERS = 1000
NUM_CATEGORIES = 20
REGIONS = ["north", "south", "east", "west"]

EVENTS_COLUMNS_GISTDB = (
    "id int4, user_id int4, category varchar, amount int4, "
    "m1 int4, m2 int4, m3 int4, m4 int4, m5 int4, region varchar, description varchar"
)
EVENTS_COLUMNS_SQLITE = (
    "id INTEGER, user_id INTEGER, category TEXT, amount INTEGER, "
    "m1 INTEGER, m2 INTEGER, m3 INTEGER, m4 INTEGER, m5 INTEGER, region TEXT, description TEXT"
)
USERS_COLUMNS_GISTDB = "id int4, name varchar"
USERS_COLUMNS_SQLITE = "id INTEGER, name TEXT"


def schema_statements(events_columns: str, users_columns: str) -> list[str]:
    return [
        f"CREATE TABLE users ({users_columns});",
        f"CREATE TABLE events ({events_columns});",
    ]


def _event_row_values(i: int, rng: random.Random) -> str:
    category = f"cat{i % NUM_CATEGORIES}"
    region = REGIONS[i % len(REGIONS)]
    metrics = [rng.randint(0, 9999) for _ in range(5)]
    return (
        f"({i},{i % NUM_USERS},'{category}',{rng.randint(0, 9999)},"
        f"{metrics[0]},{metrics[1]},{metrics[2]},{metrics[3]},{metrics[4]},"
        f"'{region}','desc_{i}')"
    )


def events_insert_statements(row_count: int, seed: int = 42):
    rng = random.Random(seed)
    start = 0
    while start < row_count:
        end = min(start + ROW_GROUP_SIZE, row_count)
        rows = ",".join(_event_row_values(i, rng) for i in range(start, end))
        yield (
            "INSERT INTO events (id, user_id, category, amount, m1, m2, m3, m4, m5, "
            f"region, description) VALUES {rows};"
        )
        start = end


def users_insert_statements(seed: int = 43):
    rng = random.Random(seed)
    rows = ",".join(f"({i},'user_{i}')" for i in range(NUM_USERS))
    yield f"INSERT INTO users (id, name) VALUES {rows};"


def wide_table_schema_and_insert(
    num_columns: int, num_rows: int, gistdb: bool, seed: int = 7
):
    table_name = f"wide_{num_columns}"
    col_type = "int4" if gistdb else "INTEGER"
    columns = ", ".join(f"c{i} {col_type}" for i in range(num_columns))
    schema = f"CREATE TABLE {table_name} ({columns});"

    rng = random.Random(seed)
    col_names = ", ".join(f"c{i}" for i in range(num_columns))
    inserts = []
    start = 0
    while start < num_rows:
        end = min(start + ROW_GROUP_SIZE, num_rows)
        rows = []
        for _ in range(start, end):
            values = ",".join(str(rng.randint(0, 9999)) for _ in range(num_columns))
            rows.append(f"({values})")
        inserts.append(
            f"INSERT INTO {table_name} ({col_names}) VALUES {','.join(rows)};"
        )
        start = end
    return table_name, schema, inserts
