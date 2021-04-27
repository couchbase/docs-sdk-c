#include <string>
#include <vector>
#include <iostream>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char* msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

struct Result {
    std::string value{};
    lcb_STATUS status{ LCB_MAX_ERROR };
};

struct SubdocResults {
    lcb_STATUS status{};
    std::vector<Result> entries{};
};

static void
sdmutate_callback(lcb_INSTANCE*, int, const lcb_RESPSUBDOC* resp)
{
    SubdocResults* results = nullptr;
    lcb_respsubdoc_cookie(resp, reinterpret_cast<void**>(&results));
    results->status = lcb_respsubdoc_status(resp);

    if (results->status != LCB_SUCCESS) {
        return;
    }

    std::size_t number_of_results = lcb_respsubdoc_result_size(resp);
    results->entries.resize(number_of_results);
    for (size_t idx = 0; idx < number_of_results; ++idx) {
        results->entries[idx].status = lcb_respsubdoc_result_status(resp, idx);
        const char* buf = nullptr;
        std::size_t buf_len = 0;
        lcb_respsubdoc_result_value(resp, idx, &buf, &buf_len);
        if (buf_len > 0) {
            results->entries[idx].value.assign(buf, buf_len);
        }
    }
}

static void
fulldoc_get_callback(lcb_INSTANCE*, int, const lcb_RESPGET* resp)
{
    const char* buf = nullptr;
    std::size_t buf_len = 0;

    check(lcb_respget_status(resp), "status of full document GET operation");
    check(lcb_respget_value(resp, &buf, &buf_len), "extract value from GET response");
    std::cout << "Document content is: " << std::string(buf, buf_len) << "\n";
}

int
main()
{
    std::string username{ "Administrator" };
    std::string password{ "password" };
    std::string connection_string{ "couchbase://localhost" };
    std::string bucket_name{ "default" };

    lcb_CREATEOPTS* create_options = nullptr;
    check(lcb_createopts_create(&create_options, LCB_TYPE_BUCKET), "build options object for lcb_create");
    check(lcb_createopts_credentials(create_options, username.c_str(), username.size(), password.c_str(), password.size()),
          "assign credentials");
    check(lcb_createopts_connstr(create_options, connection_string.c_str(), connection_string.size()), "assign connection string");
    check(lcb_createopts_bucket(create_options, bucket_name.c_str(), bucket_name.size()), "assign bucket name");

    lcb_INSTANCE* instance = nullptr;
    check(lcb_create(&instance, create_options), "create lcb_INSTANCE");
    check(lcb_createopts_destroy(create_options), "destroy options object");
    check(lcb_connect(instance), "schedule connection");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for connection");
    check(lcb_get_bootstrap_status(instance), "check bootstrap status");

    // Store a key first, so we know it will exist later on. In real production
    // environments, we'd also want to install a callback for storage operations
    // so we know if they succeeded
    std::string key{ "a_key" };

    {
        std::string value{ R"({"name":"john", "array":[1,2,3,4], "email":"john@example.com"})" };
        std::cout << "Initial document is: " << value << "\n";

        // tag::sub-doc-mutate[]
        lcb_CMDSTORE* cmd = nullptr;
        check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
        check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for UPSERT command");
        check(lcb_cmdstore_value(cmd, value.c_str(), value.size()), "assign value for UPSERT command");
        check(lcb_store(instance, nullptr, cmd), "schedule UPSERT command");
        check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        // end::sub-doc-mutate[]
    }

    lcb_install_callback(instance, LCB_CALLBACK_SDMUTATE, reinterpret_cast<lcb_RESPCALLBACK>(sdmutate_callback));

    {
        SubdocResults results;

        lcb_SUBDOCSPECS* specs = nullptr;
        check(lcb_subdocspecs_create(&specs, 3), "create SUBDOC operations container");

        // tag::path[]
        std::vector<std::string> paths{
            "array",
            "array[0]",
            "description",
        };
        // tag::array-insert[]
        std::string value_to_add{ "42" };
        check(lcb_subdocspecs_array_add_last(specs, 0, 0, paths[0].c_str(), paths[0].size(), value_to_add.c_str(), value_to_add.size()),
              "create ARRAY_ADD_LAST operation");
        // end::array-insert[]
        check(lcb_subdocspecs_counter(specs, 1, 0, paths[1].c_str(), paths[1].size(), 99), "create COUNTER operation");

        std::string value_to_upsert{ R"("just a dev")" };
        check(lcb_subdocspecs_dict_upsert(specs, 2, 0, paths[2].c_str(), paths[2].size(), value_to_upsert.c_str(), value_to_upsert.size()),
              "create DICT_UPSERT operation");
        // end::path[]
        lcb_CMDSUBDOC* cmd = nullptr;
        check(lcb_cmdsubdoc_create(&cmd), "create SUBDOC command");
        check(lcb_cmdsubdoc_key(cmd, key.c_str(), key.size()), "assign ID to SUBDOC command");
        check(lcb_cmdsubdoc_specs(cmd, specs), "assign operations to SUBDOC command");
        check(lcb_subdoc(instance, &results, cmd), "schedule SUBDOC command");
        check(lcb_cmdsubdoc_destroy(cmd), "destroy SUBDOC command");
        check(lcb_subdocspecs_destroy(specs), "destroy SUBDOC operations");

        lcb_wait(instance, LCB_WAIT_DEFAULT);

        check(results.status, "status of SUBDOC operation");
        std::size_t idx = 0;
        for (const auto& entry : results.entries) {
            std::cout << idx << ": path=\"" << paths[idx] << "\", ";
            if (entry.status == LCB_SUCCESS) {
                std::cout << "value=" << (entry.value.empty() ? "(no value)" : entry.value) << "\n";
            } else {
                std::cout << "code=" << lcb_strerror_short(entry.status) << "\n";
            }
            ++idx;
        }
    }

    lcb_install_callback(instance, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(fulldoc_get_callback));

    {
        lcb_CMDGET* cmd = nullptr;
        check(lcb_cmdget_create(&cmd), "create GET command");
        check(lcb_cmdget_key(cmd, key.c_str(), key.size()), "assign ID for GET command");
        check(lcb_get(instance, nullptr, cmd), "schedule GET command");
        check(lcb_cmdget_destroy(cmd), "destroy GET command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    // Show how to set command options!
    {
        SubdocResults results;

        lcb_SUBDOCSPECS* specs = nullptr;
        check(lcb_subdocspecs_create(&specs, 1), "create SUBDOC operations container");
        std::string path = "some.deep.path";
        std::string value = "true";
        check(lcb_subdocspecs_dict_upsert(specs, 0, 0, path.c_str(), path.size(), value.c_str(), value.size()),
              "create DICT_UPSERT command");

        lcb_CMDSUBDOC* cmd = nullptr;
        check(lcb_cmdsubdoc_create(&cmd), "create SUBDOC command");
        check(lcb_cmdsubdoc_key(cmd, key.c_str(), key.size()), "assign ID to SUBDOC command");
        check(lcb_cmdsubdoc_specs(cmd, specs), "assign operations to SUBDOC command");
        check(lcb_subdoc(instance, &results, cmd), "schedule SUBDOC command");
        check(lcb_cmdsubdoc_destroy(cmd), "destroy SUBDOC command");
        check(lcb_subdocspecs_destroy(specs), "destroy SUBDOC operations");

        lcb_wait(instance, LCB_WAIT_DEFAULT);

        std::cout << "Upsert with deep path \"" << path << "\" fails: " << lcb_strerror_short(results.entries[0].status) << "\n";
    }

    {
        SubdocResults results;

        lcb_SUBDOCSPECS* specs = nullptr;
        check(lcb_subdocspecs_create(&specs, 1), "create SUBDOC operations container");
        std::string path = "some.deep.path";
        std::string value = "true";
        check(
          lcb_subdocspecs_dict_upsert(specs, 0, LCB_SUBDOCSPECS_F_MKINTERMEDIATES, path.c_str(), path.size(), value.c_str(), value.size()),
          "create DICT_UPSERT command");

        lcb_CMDSUBDOC* cmd = nullptr;
        check(lcb_cmdsubdoc_create(&cmd), "create SUBDOC command");
        check(lcb_cmdsubdoc_key(cmd, key.c_str(), key.size()), "assign ID to SUBDOC command");
        check(lcb_cmdsubdoc_specs(cmd, specs), "assign operations to SUBDOC command");
        check(lcb_subdoc(instance, &results, cmd), "schedule SUBDOC command");
        check(lcb_cmdsubdoc_destroy(cmd), "destroy SUBDOC command");
        check(lcb_subdocspecs_destroy(specs), "destroy SUBDOC operations");

        lcb_wait(instance, LCB_WAIT_DEFAULT);

        std::cout << "Upsert with deep path \"" << path << "\" and flags: " << lcb_strerror_short(results.entries[0].status) << "\n";
    }

    {
        lcb_CMDGET* cmd = nullptr;
        check(lcb_cmdget_create(&cmd), "create GET command");
        check(lcb_cmdget_key(cmd, key.c_str(), key.size()), "assign ID for GET command");
        check(lcb_get(instance, nullptr, cmd), "schedule GET command");
        check(lcb_cmdget_destroy(cmd), "destroy GET command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    lcb_destroy(instance);
}

// OUTPUT
//
// Initial document is: {"name":"john", "array":[1,2,3,4], "email":"john@example.com"}
// 0: path="array", value=(no value)
// 1: path="array[0]", value=100
// 2: path="description", value=(no value)
// Document content is: {"name":"john", "array":[100,2,3,4,42], "email":"john@example.com","description":"just a dev"}
// Upsert with deep path "some.deep.path" fails: LCB_ERR_SUBDOC_PATH_NOT_FOUND (313)
// Upsert with deep path "some.deep.path" and flags: LCB_SUCCESS (0)
// Document content is: {"name":"john", "array":[100,2,3,4,42], "email":"john@example.com","description":"just a dev","some":{"deep":{"path":true}}}
