#include <string>
#include <vector>
#include <iostream>
#include <random>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char *msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

static void
query_callback(lcb_INSTANCE *, int, const lcb_RESPQUERY *resp)
{
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

        std::cerr << "[ERROR] failed to execute query. " << error_message << " (" << err_code
                  << ")\n";
        return;
    }

    if (lcb_respquery_is_final(resp)) {
        // We're simply notified here that the last row has already been returned.
        // no processing needed here.
        return;
    }

    // Add rows to the vector, we'll process the results in the calling code.
    const char *buf = nullptr;
    std::size_t buf_len = 0;
    lcb_respquery_row(resp, &buf, &buf_len);
    if (buf_len > 0) {
        std::vector<std::string> *rows = nullptr;
        lcb_respquery_cookie(resp, reinterpret_cast<void **>(&rows));
        rows->emplace_back(std::string(buf, buf_len));
    }
}

int
main(int, char **)
{
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string connection_string{"couchbase://localhost"};
    std::string bucket_name{"default"};

    lcb_CREATEOPTS *create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET),
            "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(),
                    password.c_str(),
                    password.size()),
            "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(),
                    connection_string.size()),
            "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.c_str(), bucket_name.size()),
            "assign bucket name");

    lcb_INSTANCE *instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    // Get a "random" value in our documents
    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<long> distrib(1, 1000000);
    long random_number = distrib(gen);

    // In real code, we'd also install a store callback so we can know if the actual storage operation was a success.
    {
        std::string key{"user:" + std::to_string(random_number)};
        std::string value{
                R"({"name": ["Brass", "Doorknob"], "email": "b@email.com", "random":)"
                        + std::to_string(random_number) +
                        "}"};

        lcb_CMDSTORE *cmd = nullptr;
        std::cout << "Will insert new document with random number " << random_number << "\n"
                  << value << "\n";
        check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
        check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for UPSERT command");
        check(lcb_cmdstore_value(cmd, value.c_str(), value.size()),
                "assign value for UPSERT command");
        check(lcb_store(instance, nullptr, cmd), "schedule UPSERT command");
        check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    std::vector<std::string> rows{};
    {
        std::string statement =
                "SELECT name, email, random FROM `" + bucket_name + "` WHERE $1 IN name LIMIT 100";
        std::string param = R"("Brass")";

        lcb_CMDQUERY *cmd = nullptr;
        check(lcb_cmdquery_create(&cmd), "create QUERY command");
        check(lcb_cmdquery_statement(cmd, statement.c_str(), statement.size()),
                "assign statement for QUERY command");
        check(lcb_cmdquery_positional_param(cmd, param.c_str(), param.size()),
                "add positional parameter for QUERY command");
        check(lcb_cmdquery_consistency(cmd, LCB_QUERY_CONSISTENCY_REQUEST),
                "assign consistency level for QUERY command");
        check(lcb_cmdquery_callback(cmd, query_callback), "assign callback for QUERY command");
        check(lcb_query(instance, &rows, cmd), "schedule QUERY command");
        check(lcb_cmdquery_destroy(cmd), "destroy QUERY command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }
    std::cout << "Query returned " << rows.size() << " rows\n";

    // To demonstrate the CONSISTENCY_REQUEST feature, we check each row for the "random" value.
    //
    // For clarity, we print out only the row's "Random" field. When the CONSISTENCY_REQUEST feature is enabled, one of the results should
    // contain the newest random number (the value of random_number). When disabled, the row may or may not appear.
    size_t idx = 0;
    std::string prop_name = R"("random":)";
    for (auto &row : rows) {
        size_t begin_pos = row.find(prop_name);
        size_t end_pos = row.find_first_of("},", begin_pos);
        std::string field = row.substr(begin_pos + prop_name.size(), end_pos - begin_pos);
        long row_random_number = std::stol(field);
        std::cout << idx++ << " row has random number: " << row_random_number
                  << (row_random_number == random_number ? " <---\n" : "\n");
    }

    lcb_destroy(instance);
}

// OUTPUT
//
// Will insert new document with random number 323863
// {"name": ["Brass", "Doorknob"], "email": "b@email.com", "random":323863}
// Query returned 14 rows
// 0 row has random number: 242901
// 1 row has random number: 323863 <---
// 2 row has random number: 436510
// 3 row has random number: 449062
// 4 row has random number: 450020
// 5 row has random number: 463949
// 6 row has random number: 470900
// 7 row has random number: 498086
// 8 row has random number: 508091
// 9 row has random number: 594145
// 10 row has random number: 659117
// 11 row has random number: 667662
// 12 row has random number: 697572
// 13 row has random number: 997896
