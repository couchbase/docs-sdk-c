/*
 * REQUIREMENTS
 *   Running couchbase server on localhost with:
 *   user: some-user/some-password
 *         with some RBAC (just used Full Admin for now)
 *   bucket "default"
 *   CREATE PRIMARY INDEX ON `default`:`default`
 */

#include <libcouchbase/couchbase.h>
#include <string>
#include <vector>
#include <iostream>
#include <random>

static void
check(lcb_STATUS err, const char *msg) {
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

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

static void
storage_callback(lcb_INSTANCE *, int, const lcb_RESPSTORE *resp) {
    check(lcb_respstore_status(resp), "get status of STORE operation in callback");

    lcb_MUTATION_TOKEN *mutation_token = nullptr;
    lcb_respstore_cookie(resp, reinterpret_cast<void **>(&mutation_token));
    check(lcb_respstore_mutation_token(resp, mutation_token),
          "extract mutation token from STORE operation response");
}

int
main(int, char **) {
    std::string username{"some-user"};
    std::string password{"some-password"};
    std::string bucket_name{"default"};
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

    // Get a "random" value in our documents
    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<long> distrib(1, 1000000);
    long random_number = distrib(gen);

    // Install the storage callback which will be used to retrieve the mutation token
    lcb_install_callback(instance, LCB_CALLBACK_STORE,
                         reinterpret_cast<lcb_RESPCALLBACK>(storage_callback));

    lcb_MUTATION_TOKEN mutation_token{};

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
        check(lcb_cmdstore_key(cmd, key.data(), key.size()), "assign ID for UPSERT command");
        check(lcb_cmdstore_value(cmd, value.data(), value.size()),
              "assign value for UPSERT command");
        check(lcb_store(instance, &mutation_token, cmd), "schedule UPSERT command");
        check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    std::vector<std::string> rows{};
    {
        std::string statement =
                "SELECT name, email, random FROM `" + bucket_name + "` WHERE $1 IN name LIMIT 100";

        std::string parameters_json = "[\"Brass\"]";

        lcb_CMDQUERY *cmd = nullptr;
        check(lcb_cmdquery_create(&cmd), "create QUERY command");
        check(lcb_cmdquery_statement(cmd, statement.data(), statement.size()),
              "assign statement for QUERY command");
        check(lcb_cmdquery_positional_params
                      (cmd,
                       parameters_json.data(),
                       parameters_json.length()),
              "set QUERY positional parameters");
        check(lcb_cmdquery_consistency_token_for_keyspace(cmd, bucket_name.data(),
                                                          bucket_name.size(),
                                                          &mutation_token),
              "add consistency token for QUERY command");
        check(lcb_cmdquery_callback(cmd, query_callback), "assign callback for QUERY command");
        check(lcb_query(instance, &rows, cmd), "schedule QUERY command");
        check(lcb_cmdquery_destroy(cmd), "destroy QUERY command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }
    std::cout << "Query returned " << rows.size() << " rows\n";

    // To demonstrate the at_plus feature, we check each row for the "random" value.
    //
    // For clarity, we print out only the row's "Random" field. When the at_plus feature is enabled, one of the results should contain
    // the newest random number (the value of random_number). When disabled, the row may or may not appear.
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
// Will insert new document with random number 594145
// {"name": ["Brass", "Doorknob"], "email": "brass@example.com", "random":594145}
// Query returned 12 rows
// 0 row has random number: 242901
// 1 row has random number: 436510
// 2 row has random number: 449062
// 3 row has random number: 450020
// 4 row has random number: 463949
// 5 row has random number: 470900
// 6 row has random number: 498086
// 7 row has random number: 508091
// 8 row has random number: 594145 <---
// 9 row has random number: 659117
// 10 row has random number: 667662
// 11 row has random number: 697572
