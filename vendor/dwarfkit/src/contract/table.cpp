#include <dwarfkit/contract/table.hpp>

#include <algorithm>

namespace dwarfkit {

// ---- TableCursor ------------------------------------------------------------

Result<void> TableCursor::init(TableCursor& cursor, const TableCursorArgs& args) {
    cursor.abi = args.abi;
    cursor.client = args.client;
    // the default parameters for a get_table_rows call
    cursor.params = json{{"json", false}, {"limit", 1000}};
    for (const auto& item : args.params.items()) {
        cursor.params[item.key()] = item.value();
    }
    if (args.maxRows) {
        cursor.maxRows_ = *args.maxRows;
    }
    const std::string tableName = cursor.params.value("table", "");
    const auto table =
        std::find_if(cursor.abi.tables.begin(), cursor.abi.tables.end(),
                     [&](const auto& t) { return t.name == Name::from(tableName); });
    if (table == cursor.abi.tables.end()) {
        return err(ErrorKind::NotFound, "Table not found");
    }
    cursor.type = table->type;
    return {};
}

void TableCursor::reset() {
    next_key_ = json();
    endReached_ = false;
    rowsCount_ = 0;
}

Result<std::vector<json>> TableCursor::all() {
    std::vector<json> rows;
    while (true) {
        DK_TRY(batch, next());
        rows.insert(rows.end(), batch.begin(), batch.end());
        if (batch.empty() || next_key_.is_null() ||
            (next_key_.is_string() && next_key_.get_ref<const std::string&>().empty())) {
            break;
        }
    }
    return rows;
}

json TableCursor::getTableRowsParams(int64_t rowsPerAPIRequest) const {
    // Set the lower_bound, and override if the cursor has a next_key value
    json lower_bound = params.contains("lower_bound") ? params["lower_bound"] : json();
    if (!next_key_.is_null()) {
        lower_bound = next_key_;
    }

    // Determine the maximum number of remaining rows for the cursor
    const int64_t rowsRemaining = maxRows_ - rowsCount_;

    // The lowest of rows remaining, rows per request, and the params limit
    const int64_t limit =
        std::min({rowsRemaining, rowsPerAPIRequest, params.value("limit", int64_t(1000))});

    json query = params;
    query["limit"] = limit;
    const json wrappedLower = wrapIndexValue(lower_bound);
    if (!wrappedLower.is_null()) {
        query["lower_bound"] = wrappedLower;
    } else {
        query.erase("lower_bound");
    }
    const json wrappedUpper =
        wrapIndexValue(params.contains("upper_bound") ? params["upper_bound"] : json());
    if (!wrappedUpper.is_null()) {
        query["upper_bound"] = wrappedUpper;
    } else {
        query.erase("upper_bound");
    }
    return query;
}

// ---- TableRowCursor ---------------------------------------------------------

Result<TableRowCursor> TableRowCursor::from(const TableCursorArgs& args) {
    TableRowCursor cursor;
    DK_CHECK(init(cursor, args));
    return cursor;
}

Result<std::vector<json>> TableRowCursor::next(int64_t rowsPerAPIRequest) {
    // If the cursor has deemed its at the end, return an empty array
    if (endReached_) {
        return std::vector<json>{};
    }

    // Assemble the query params and execute the query
    const json query = getTableRowsParams(rowsPerAPIRequest);
    DK_TRY(result, client->v1.chain.get_table_rows(query));

    // Decode the rows when hex data was returned
    const bool requiresDecoding = query.value("json", false) == false;
    std::vector<json> rows;
    for (const auto& row : result.rows) {
        if (requiresDecoding && row.is_string()) {
            DK_TRY(data, Bytes::from(row.get<std::string>()));
            DK_TRY(decoded, Serializer::decode(data.array, type, abi));
            rows.push_back(decoded);
        } else {
            rows.push_back(row);
        }
    }

    // Persist cursor state for subsequent calls
    next_key_ = result.next_key;
    rowsCount_ += static_cast<int64_t>(rows.size());

    // Determine if we've reached the end of the cursor
    const bool noNextKey =
        result.next_key.is_null() ||
        (result.next_key.is_string() && result.next_key.get_ref<const std::string&>().empty());
    if (noNextKey || rows.empty() || rowsCount_ == maxRows_) {
        endReached_ = true;
    }

    return rows;
}

// ---- TableScopeCursor -------------------------------------------------------

Result<TableScopeCursor> TableScopeCursor::from(const TableCursorArgs& args) {
    TableScopeCursor cursor;
    DK_CHECK(init(cursor, args));
    return cursor;
}

Result<std::vector<json>> TableScopeCursor::next(int64_t rowsPerAPIRequest) {
    if (endReached_) {
        return std::vector<json>{};
    }

    json lower_bound = params.contains("lower_bound") ? params["lower_bound"] : json();
    if (!next_key_.is_null()) {
        lower_bound = next_key_;
    }
    const int64_t rowsRemaining = maxRows_ - rowsCount_;
    const int64_t limit =
        std::min({rowsRemaining, rowsPerAPIRequest, params.value("limit", int64_t(1000))});

    json query = {{"code", params.value("code", "")},
                  {"table", params.value("table", "")},
                  {"limit", limit}};
    if (!lower_bound.is_null()) {
        query["lower_bound"] =
            lower_bound.is_string() ? lower_bound.get<std::string>() : lower_bound.dump();
    }
    if (params.contains("upper_bound") && !params["upper_bound"].is_null()) {
        const json& upper = params["upper_bound"];
        query["upper_bound"] = upper.is_string() ? upper.get<std::string>() : upper.dump();
    }

    DK_TRY(result, client->v1.chain.get_table_by_scope(query));

    std::vector<json> rows;
    for (const auto& row : result.rows) {
        rows.push_back(Serializer::objectify(row));
    }

    // Persist cursor state (get_table_by_scope pages with `more`)
    next_key_ = result.more.empty() ? json() : json(result.more);
    rowsCount_ += static_cast<int64_t>(rows.size());

    if (result.more.empty() || rows.empty() || rowsCount_ == maxRows_) {
        endReached_ = true;
    }

    return rows;
}

// ---- Table ------------------------------------------------------------------

Result<Table> Table::from(const TableParams& params) {
    Table table;
    table.abi = params.abi;
    table.account = params.account;
    table.client = params.client;
    table.name = params.name;
    table.debug = params.debug;
    table.defaultScope = params.defaultScope;
    if (params.defaultRowLimit) {
        table.defaultRowLimit = *params.defaultRowLimit;
    }
    const auto found = std::find_if(table.abi.tables.begin(), table.abi.tables.end(),
                                    [&](const auto& t) { return table.name == t.name; });
    if (found == table.abi.tables.end()) {
        return err(ErrorKind::NotFound, "Table " + table.name.toString() + " not found in ABI");
    }
    table.tableABI = *found;
    return table;
}

Result<json> Table::resolveScope(const json& scope) const {
    const json value = isAbsentScope(scope) ? defaultScope : scope;
    if (isAbsentScope(value)) {
        return json(account.toString());
    }
    return wrapScopeValue(value);
}

namespace {

// the upstream client infers key_type from the runtime type of the bounds;
// with json bounds a number means i64 and anything else stays a name (pass
// key_type explicitly for i128/sha256/ripemd160 lookups)
std::string inferKeyType(const QueryParams& params, const json& extraBound = {}) {
    if (params.key_type) {
        return *params.key_type;
    }
    const json& bound = !params.from.is_null()   ? params.from
                        : !params.to.is_null()   ? params.to
                        : !extraBound.is_null()  ? extraBound
                                                 : json();
    if (bound.is_number()) {
        return "i64";
    }
    return "name";
}

}  // namespace

Result<json> Table::buildParams(const QueryParams& params, std::optional<int64_t> limit) const {
    // key insertion order mirrors the upstream construction so the request
    // bodies (and their fixture hashes) match: the cursor path merges these
    // into {json, limit} defaults, the get path sends them directly
    std::optional<std::string> indexPosition = params.index_position;
    if (params.index) {
        const json mapping = getFieldToIndex();
        if (!mapping.contains(*params.index)) {
            // Nearly all contract ABIs are missing data to appropriately map
            // this data. See: https://github.com/AntelopeIO/cdt/issues/197
            return err(ErrorKind::Invalid,
                       "Field " + *params.index +
                           " is not listed in the ABI under key_names/key_types. Try using "
                           "'index_position' instead.");
        }
        indexPosition = mapping[*params.index]["index_position"].get<std::string>();
    }
    json rv = json::object();
    rv["table"] = name.toString();
    rv["code"] = account.toString();
    DK_TRY(scope, resolveScope(params.scope));
    rv["scope"] = scope;
    const bool wantJson = params.asJson.value_or(false) || debug;
    const bool getPath = limit.has_value();
    if (!getPath) {
        // cursor path: json first via the cursor defaults; only override
        // when decoded rows were requested
        if (wantJson) {
            rv["json"] = true;
        }
        if (indexPosition) {
            rv["index_position"] = *indexPosition;
        }
        rv["key_type"] = inferKeyType(params);
        const json lower = wrapIndexValue(params.from);
        if (!lower.is_null()) {
            rv["lower_bound"] = lower;
        }
        const json upper = wrapIndexValue(params.to);
        if (!upper.is_null()) {
            rv["upper_bound"] = upper;
        }
        rv["limit"] = params.rowsPerAPIRequest.value_or(defaultRowLimit);
        if (params.reverse) {
            rv["reverse"] = *params.reverse;
        }
    } else {
        rv["limit"] = *limit;
        const json lower = wrapIndexValue(params.from);
        if (!lower.is_null()) {
            rv["lower_bound"] = lower;
        }
        const json upper = wrapIndexValue(params.to);
        if (!upper.is_null()) {
            rv["upper_bound"] = upper;
        }
        if (indexPosition) {
            rv["index_position"] = *indexPosition;
        }
        rv["key_type"] = inferKeyType(params);
        rv["json"] = wantJson;
        if (params.reverse) {
            rv["reverse"] = *params.reverse;
        }
    }
    return rv;
}

Result<TableRowCursor> Table::query(const QueryParams& params) const {
    DK_TRY(tableRowsParams, buildParams(params, std::nullopt));
    TableCursorArgs args;
    args.abi = abi;
    args.client = client;
    args.params = tableRowsParams;
    args.maxRows = params.maxRows;
    return TableRowCursor::from(args);
}

Result<std::optional<json>> Table::get(const json& value, const QueryParams& params) const {
    QueryParams getParams = params;
    if (!value.is_null()) {
        getParams.from = value;
        getParams.to = value;
    }
    DK_TRY(tableRowsParams, buildParams(getParams, 1));
    DK_TRY(result, client->v1.chain.get_table_rows(tableRowsParams));
    if (result.rows.empty()) {
        return std::optional<json>{};
    }
    json row = result.rows[0];
    // Debug (or json) mode returns decoded rows already
    if (tableRowsParams.value("json", false)) {
        return std::optional(row);
    }
    if (row.is_string()) {
        DK_TRY(data, Bytes::from(row.get<std::string>()));
        DK_TRY(decoded, Serializer::decode(data.array, tableABI.type, abi));
        row = decoded;
    }
    return std::optional(row);
}

Result<TableRowCursor> Table::first(int64_t maxRows, const QueryParams& params) const {
    QueryParams withMax = params;
    withMax.maxRows = maxRows;
    return query(withMax);
}

Result<std::vector<json>> Table::all(const QueryParams& params) const {
    DK_TRY(cursor, query(params));
    return cursor.all();
}

json Table::getFieldToIndex() const {
    json rv = json::object();
    for (size_t i = 0; i < tableABI.key_names.size(); i++) {
        rv[tableABI.key_names[i]] = json{{"type", tableABI.key_types[i]},
                                         {"index_position", indexPositionInWords(i)}};
    }
    return rv;
}

Result<TableScopeCursor> Table::scopes(const QueryParams& params) const {
    json tableRowsParams = {{"code", account.toString()}, {"table", name.toString()}};
    const json lower = wrapIndexValue(params.from);
    if (!lower.is_null()) {
        tableRowsParams["lower_bound"] = lower;
    }
    const json upper = wrapIndexValue(params.to);
    if (!upper.is_null()) {
        tableRowsParams["upper_bound"] = upper;
    }
    tableRowsParams["limit"] = params.rowsPerAPIRequest.value_or(defaultRowLimit);
    if (params.reverse) {
        tableRowsParams["reverse"] = *params.reverse;
    }
    TableCursorArgs args;
    args.abi = abi;
    args.client = client;
    args.params = tableRowsParams;
    args.maxRows = params.maxRows;
    return TableScopeCursor::from(args);
}

}  // namespace dwarfkit
