/*
 * REQUIREMENTS
 *   Running couchbase server on localhost with:
 *   user: some-user/some-password
 *         with some RBAC (just used Full Admin for now)
 *   bucket "default"
 *   CREATE PRIMARY INDEX ON `default`:`default`
 */

#include <libcouchbase/couchbase.h>
#include <vector>
#include <string>
#include <iostream>

static void
check(lcb_STATUS err, const char *msg) {
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

struct Rows {
    std::vector<std::string> rows{};
    std::string metadata{};
};

static void
query_callback(lcb_INSTANCE *, int, const lcb_RESPQUERY *resp) {
    lcb_STATUS status = lcb_respquery_status(resp);
    if (status != LCB_SUCCESS) {
        const lcb_QUERY_ERROR_CONTEXT *ctx;
        lcb_respquery_error_context(resp, &ctx);

        uint32_t err_code = 0;
        lcb_errctx_query_first_error_code(ctx, &err_code);

        const char *err_msg = nullptr;
        size_t err_msg_len = 0;
        lcb_errctx_query_first_error_message(ctx, &err_msg, &err_msg_len);
        std::string error_message{};
        if (err_msg_len > 0) {
            error_message.assign(err_msg, err_msg_len);
        }

        std::cerr << "[ERROR] failed to execute query " << lcb_strerror_short(status) << ". "
                  << error_message << " ("
                  << err_code << ")\n";
        return;
    }

    const char *buf = nullptr;
    std::size_t buf_len = 0;
    lcb_respquery_row(resp, &buf, &buf_len);
    if (buf_len > 0) {
        Rows *result = nullptr;
        lcb_respquery_cookie(resp, reinterpret_cast<void **>(&result));
        if (lcb_respquery_is_final(resp)) {
            result->metadata.assign(buf, buf_len);
        } else {
            result->rows.emplace_back(std::string(buf, buf_len));
        }
    }
}

static void
query_city(lcb_INSTANCE *instance, const std::string &bucket_name, const std::string &city) {
    Rows result{};

    // tag::placeholder[]
    std::string statement =
            "SELECT airportname, city, country FROM `" + bucket_name
            + R"(` WHERE type=$1 AND city=$2)";

    lcb_CMDQUERY *cmd = nullptr;
    check(lcb_cmdquery_create(&cmd), "create QUERY command");
    check(lcb_cmdquery_statement(cmd, statement.data(), statement.size()),
          "assign statement for QUERY command");

    std::string parameters_json = "[\"airport\", \"" + city + "\"]"; // production code should use JSON encoding library

    check(lcb_cmdquery_positional_params(
                  cmd,
                  parameters_json.data(),
                  parameters_json.length()),
          "add positional parameters for QUERY comand");
    // Enable using prepared (optimized) statements
    check(lcb_cmdquery_adhoc(cmd, false), "enable prepared statements for QUERY command");
    check(lcb_cmdquery_callback(cmd, query_callback), "assign callback for QUERY command");
    check(lcb_query(instance, &result, cmd), "schedule QUERY command");
    check(lcb_cmdquery_destroy(cmd), "destroy QUERY command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    // end::placeholder[]

    std::cout << "\n--- Query returned " << result.rows.size() << " rows for " << city << std::endl;
    for (const auto &row : result.rows) {
        std::cout << row << "\n";
    }
}

int
main(int, char **) {
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string bucket_name{"travel-sample"};
    std::string connection_string{"couchbase://localhost"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET),
          "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.data(), username.size(),
                                     password.data(),
                                     password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.data(),
                                 connection_string.size()),
          "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.data(), bucket_name.size()),
          "assign bucket name");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    query_city(instance, bucket_name, "Reno");
    query_city(instance, bucket_name, "Dallas");
    query_city(instance, bucket_name, "Los Angeles");

    lcb_destroy(instance);
}

// OUTPUT
//
// --- Query returned 1 rows for Reno
// {"airportname":"Reno Tahoe Intl","city":"Reno","country":"United States"}
//
// --- Query returned 3 rows for Dallas
// {"airportname":"Dallas Love Fld","city":"Dallas","country":"United States"}
// {"airportname":"Dallas Executive Airport","city":"Dallas","country":"United States"}
// {"airportname":"Fort Worth NAS","city":"Dallas","country":"United States"}
//
// --- Query returned 2 rows for Los Angeles
// {"airportname":"Whiteman Airport","city":"Los Angeles","country":"United States"}
// {"airportname":"Los Angeles Intl","city":"Los Angeles","country":"United States"}
