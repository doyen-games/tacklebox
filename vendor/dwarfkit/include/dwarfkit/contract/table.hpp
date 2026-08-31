// Port of contract src/contract/{table,table-cursor,row-cursor,scope-cursor}.
// Rows are json (BLUEPRINT.md 6.7); rowType-based typed rows convert at the
// call site. The async iterator protocol becomes next()/all() loops.
#pragma once

#include <dwarfkit/contract/utils.hpp>

namespace dwarfkit {

struct QueryParams {
    std::optional<std::string> index;
    std::optional<std::string> index_position;
    // a name/string or uint64 scope; null falls back to the table default
    json scope;
    std::optional<std::string> key_type;
    json from;  // lower bound index value
    json to;    // upper bound index value
    std::optional<int64_t> maxRows;
    std::optional<int64_t> rowsPerAPIRequest;
    std::optional<bool> reverse;
    // decode rows through the node (debug) or objectify on get()
    std::optional<bool> asJson;
};

struct TableCursorArgs {
    ABI abi;
    std::shared_ptr<APIClient> client;
    // parameters for the v1/chain/get_table_rows call
    json params;
    std::optional<int64_t> maxRows;
};

class TableCursor {
public:
    static constexpr int64_t unlimited = 9007199254740991;  // Number.MAX_SAFE_INTEGER

    ABI abi;
    // the type of the table, as defined in the ABI
    std::string type;
    json params;
    std::shared_ptr<APIClient> client;

    // the next key to query against lower_bounds when iterating
    json nextkey() const { return next_key_; }
    bool endReached() const { return endReached_; }

    // Fetch the next batch of rows from the cursor.
    virtual Result<std::vector<json>> next(int64_t rowsPerAPIRequest = unlimited) = 0;

    // Reset the internal state of the cursor.
    void reset();

    // Fetch all rows by calling next() until the end is reached.
    Result<std::vector<json>> all();

    virtual ~TableCursor() = default;

protected:
    static Result<void> init(TableCursor& cursor, const TableCursorArgs& args);
    // the query for the get_table_rows endpoint
    json getTableRowsParams(int64_t rowsPerAPIRequest = unlimited) const;

    json next_key_;
    bool endReached_ = false;
    int64_t rowsCount_ = 0;
    int64_t maxRows_ = unlimited;
};

class TableRowCursor final : public TableCursor {
public:
    static Result<TableRowCursor> from(const TableCursorArgs& args);
    Result<std::vector<json>> next(int64_t rowsPerAPIRequest = unlimited) override;
};

class TableScopeCursor final : public TableCursor {
public:
    static Result<TableScopeCursor> from(const TableCursorArgs& args);
    Result<std::vector<json>> next(int64_t rowsPerAPIRequest = unlimited) override;
};

struct TableParams {
    ABI abi;
    Name account;
    std::shared_ptr<APIClient> client;
    Name name;
    bool debug = false;
    std::optional<int64_t> defaultRowLimit;
    json defaultScope;
};

// A table in a smart contract, with methods for querying its rows.
class Table {
public:
    static Result<Table> from(const TableParams& params);

    ABI abi;
    Name account;
    std::shared_ptr<APIClient> client;
    Name name;
    ABI::Table tableABI;
    bool debug = false;
    json defaultScope;
    int64_t defaultRowLimit = 1000;

    Result<TableRowCursor> query(const QueryParams& params = {}) const;
    // A single row matching the value, nullopt when none matches.
    Result<std::optional<json>> get(const json& value = {},
                                    const QueryParams& params = {}) const;
    Result<TableRowCursor> first(int64_t maxRows, const QueryParams& params = {}) const;
    Result<std::vector<json>> all(const QueryParams& params = {}) const;
    Result<TableScopeCursor> scopes(const QueryParams& params = {}) const;

    // field name to {type, index_position} mapping from the table ABI
    json getFieldToIndex() const;

private:
    Result<json> buildParams(const QueryParams& params, std::optional<int64_t> limit) const;
    Result<json> resolveScope(const json& scope) const;
};

}  // namespace dwarfkit
