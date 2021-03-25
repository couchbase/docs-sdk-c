#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>

#include <libcouchbase/couchbase.h>
#include <cstring>

constexpr static int number_of_threads = 20;

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
    std::uint64_t cas{ 0 };
    lcb_STATUS rc{ LCB_SUCCESS };
};

static void
get_callback(lcb_INSTANCE*, int, const lcb_RESPGET* resp)
{
    Result* result = nullptr;
    lcb_respget_cookie(resp, reinterpret_cast<void**>(&result));

    result->rc = lcb_respget_status(resp);
    check(lcb_respget_cas(resp, &result->cas), "extract CAS from GET response");

    const char* buf = nullptr;
    std::size_t buf_len = 0;
    check(lcb_respget_value(resp, &buf, &buf_len), "extract value from GET response");
    result->value.assign(buf, buf_len);
}

static void
replace_callback(lcb_INSTANCE*, int, const lcb_RESPSTORE* resp)
{
    Result* result = nullptr;
    lcb_respstore_cookie(resp, reinterpret_cast<void**>(&result));

    result->rc = lcb_respstore_status(resp);
    check(lcb_respstore_cas(resp, &result->cas), "extract CAS from STORE response");
}

static lcb_INSTANCE*
create_instance()
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

    lcb_install_callback(instance, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(get_callback));
    lcb_install_callback(instance, LCB_CALLBACK_STORE, reinterpret_cast<lcb_RESPCALLBACK>(replace_callback));
    return instance;
}

static std::string
add_item_to_list(const std::string& old_list, const std::string& new_item)
{
    // Remove the trailing ']'
    std::string newval = old_list.substr(0, old_list.size() - 1);

    if (old_list.size() != 2) {
        // The current value is not an empty list. Insert preceding comma
        newval += ",";
    }

    newval += new_item;
    newval += "]";
    return newval;
}

// Boilerplate for storing our initial list as '[]'
static void
store_initial_list(lcb_INSTANCE* instance, const std::string& id)
{
    std::string initial_document{ "[]" };
    Result result;

    lcb_CMDSTORE* cmd = nullptr;
    check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
    check(lcb_cmdstore_key(cmd, id.c_str(), id.size()), "assign ID for UPSERT command");
    check(lcb_cmdstore_value(cmd, initial_document.c_str(), initial_document.size()), "assign value for UPSERT command");
    check(lcb_store(instance, &result, cmd), "schedule UPSERT command");
    check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
    check(lcb_wait(instance, LCB_WAIT_DEFAULT), "wait for initial UPSERT command");
    check(result.rc, "could not store initial list document");
}

// Counts the number of items in the list
static int
count_list_items(const std::string& s)
{
    size_t pos = 0;
    int number_of_items = 0;
    while (pos != std::string::npos) {
        pos = s.find(',', pos ? pos + 1 : pos);
        if (pos != std::string::npos) {
            number_of_items++;
        }
    }
    if (number_of_items > 0) {
        // Add the last item, which lacks a comma
        number_of_items++;
    } else if (s.size() > 2) {
        number_of_items = 1;
    }
    return number_of_items;
}

int
main()
{
    std::string document_id{ "a_list" };

    lcb_INSTANCE* instance = create_instance();
    store_initial_list(instance, document_id);

    std::vector<std::thread> threads;
    threads.reserve(number_of_threads);
    std::cout << "Using " << number_of_threads << " threads\n";

    std::cout << "\nFirst, try to add items unsafely without CAS optimistic locking\n";
    for (int i = 0; i < number_of_threads; i++) {
        std::stringstream ss;
        ss << "item_" << i;
        std::string item_value = ss.str();
        // This is "unsafe" implementation of the worker
        // Every thread reads the document, builds new value, and replaces without CAS
        threads.emplace_back([document_id, item_value]() {
            lcb_INSTANCE* local_instance = create_instance();
            Result result;

            {
                lcb_CMDGET* cmd = nullptr;
                check(lcb_cmdget_create(&cmd), "create GET command");
                check(lcb_cmdget_key(cmd, document_id.c_str(), document_id.size()), "assign ID for GET command");
                check(lcb_get(local_instance, &result, cmd), "schedule GET command");
                check(lcb_cmdget_destroy(cmd), "destroy GET command");
                lcb_wait(local_instance, LCB_WAIT_DEFAULT);
                check(result.rc, "could not find list document");
            }

            std::string new_value = add_item_to_list(result.value, item_value);
            // tag::cas[]
            result = {}; // reset result object
            {
                lcb_CMDSTORE* cmd = nullptr;
                check(lcb_cmdstore_create(&cmd, LCB_STORE_REPLACE), "create REPLACE command");
                check(lcb_cmdstore_key(cmd, document_id.c_str(), document_id.size()), "assign ID for REPLACE command");
                check(lcb_cmdstore_value(cmd, new_value.c_str(), new_value.size()), "assign value for REPLACE command");
                check(lcb_store(local_instance, &result, cmd), "schedule REPLACE command");
                check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
                lcb_wait(local_instance, LCB_WAIT_DEFAULT);

                if (result.rc != LCB_SUCCESS) {
                    std::stringstream msg;
                    msg << "failed to append " << item_value << ": " << lcb_strerror_short(result.rc) << "\n";
                    std::cout << msg.str();
                }
            }
            // end::cas[]

            lcb_destroy(local_instance);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    {
        // Verify entries
        Result result;
        {
            lcb_CMDGET* cmd = nullptr;
            check(lcb_cmdget_create(&cmd), "create GET command");
            check(lcb_cmdget_key(cmd, document_id.c_str(), document_id.size()), "assign ID for GET command");
            check(lcb_get(instance, &result, cmd), "schedule GET command");
            check(lcb_cmdget_destroy(cmd), "destroy GET command");
            lcb_wait(instance, LCB_WAIT_DEFAULT);
            check(result.rc, "could not find list document");
        }
        int number_of_items = count_list_items(result.value);
        std::cout << "New value: " << result.value << "\n";
        std::cout << "Have " << number_of_items << " in the list\n";
        if (number_of_items != number_of_threads) {
            std::cout << "Some items were cut off because of concurrent mutations. Expected " << number_of_threads << "!\n";
        }
    }

    // Try it again using the safe version
    threads.clear();
    std::cout << "\nNow insert items using CAS\n";

    // First reset the list
    store_initial_list(instance, document_id);

    for (int i = 0; i < number_of_threads; i++) {
        std::stringstream ss;
        ss << "item_" << i;
        std::string item_value = ss.str();
        // This is "unsafe" implementation of the worker
        // Every thread reads the document, builds new value, and replaces without CAS
        threads.emplace_back([document_id, item_value]() {
            lcb_INSTANCE* local_instance = create_instance();
            while (true) {
                uint64_t cas = 0;
                Result result;

                {
                    lcb_CMDGET* cmd = nullptr;
                    check(lcb_cmdget_create(&cmd), "create GET command");
                    check(lcb_cmdget_key(cmd, document_id.c_str(), document_id.size()), "assign ID for GET command");
                    check(lcb_get(local_instance, &result, cmd), "schedule GET command");
                    check(lcb_cmdget_destroy(cmd), "destroy GET command");
                    lcb_wait(local_instance, LCB_WAIT_DEFAULT);
                    check(result.rc, "could not find list document");
                    cas = result.cas;
                }

                std::string new_value = add_item_to_list(result.value, item_value);
                result = {}; // reset result object
                {
                    lcb_CMDSTORE* cmd = nullptr;
                    check(lcb_cmdstore_create(&cmd, LCB_STORE_REPLACE), "create REPLACE command");
                    check(lcb_cmdstore_key(cmd, document_id.c_str(), document_id.size()), "assign ID for REPLACE command");
                    check(lcb_cmdstore_value(cmd, new_value.c_str(), new_value.size()), "assign value for REPLACE command");
                    check(lcb_cmdstore_cas(cmd, cas), "assign CAS value for REPLACE command");
                    check(lcb_store(local_instance, &result, cmd), "schedule REPLACE command");
                    check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
                    lcb_wait(local_instance, LCB_WAIT_DEFAULT);

                    if (result.rc == LCB_SUCCESS) {
                        break;
                    } else if (result.rc == LCB_ERR_DOCUMENT_EXISTS) {
                        std::stringstream msg;
                        msg << "CAS mismatch for " << item_value << ". Retrying...\n";
                        std::cout << msg.str();
                        continue;
                    } else {
                        std::stringstream msg;
                        msg << "failed to append " << item_value << ": " << lcb_strerror_short(result.rc) << "\n";
                        std::cout << msg.str();
                    }
                }
            }

            lcb_destroy(local_instance);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    {
        // Verify entries
        Result result;
        {
            lcb_CMDGET* cmd = nullptr;
            check(lcb_cmdget_create(&cmd), "create GET command");
            check(lcb_cmdget_key(cmd, document_id.c_str(), document_id.size()), "assign ID for GET command");
            check(lcb_get(instance, &result, cmd), "schedule GET command");
            check(lcb_cmdget_destroy(cmd), "destroy GET command");
            lcb_wait(instance, LCB_WAIT_DEFAULT);
            check(result.rc, "could not find list document");
        }
        std::cout << "New value: " << result.value << "\n";
        std::cout << "Have " << count_list_items(result.value) << " in the list\n";
    }

    lcb_destroy(instance);
    return 0;
}

// OUTPUT
// ------
//
// Using 20 threads
//
// First, try to add items unsafely without CAS optimistic locking
// New value: [item_2,item_10,item_8,item_7,item_12,item_4,item_5,item_6,item_14,item_19,item_9,item_11,item_17,item_16,item_13]
// Have 15 in the list
// Some items were cut off because of concurrent mutations. Expected 20!
//
// Now insert items using CAS
// CAS mismatch for item_12. Retrying...
// CAS mismatch for item_0. Retrying...
// CAS mismatch for item_15. Retrying...
// CAS mismatch for item_15. Retrying...
// CAS mismatch for item_1. Retrying...
// CAS mismatch for item_17. Retrying...
// CAS mismatch for item_8. Retrying...
// CAS mismatch for item_8. Retrying...
// New value: [item_4,item_13,item_2,item_9,item_12,item_3,item_0,item_10,item_6,item_15,item_1,item_17,item_8,item_16,item_19,item_5,item_7,item_11,item_14,item_18]
// Have 20 in the list
