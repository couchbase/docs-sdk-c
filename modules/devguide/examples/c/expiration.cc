#include <string>
#include <iostream>
#include <thread>

#include <libcouchbase/couchbase.h>

static void
check(lcb_STATUS err, const char* msg)
{
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

static void
store_callback(lcb_INSTANCE*, int, const lcb_RESPSTORE* resp)
{
    lcb_STATUS status = lcb_respstore_status(resp);
    if (status == LCB_SUCCESS) {
        std::cout << "STORE succeed\n";
    } else {
        std::cout << "STORE failed: " << lcb_strerror_short(status) << "\n";
    }
}

static void
store_key(lcb_INSTANCE* instance, const std::string& key, const std::string& value, std::chrono::seconds expiry = {})
{
    lcb_CMDSTORE* cmd = nullptr;
    // tag::expiration[]
    check(lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT), "create UPSERT command");
    check(lcb_cmdstore_key(cmd, key.c_str(), key.size()), "assign ID for UPSERT command");
    check(lcb_cmdstore_value(cmd, value.c_str(), value.size()), "assign value for UPSERT command");
    check(lcb_cmdstore_expiry(cmd, static_cast<std::uint32_t>(expiry.count())), "assign expiration to UPSERT command");
    check(lcb_store(instance, nullptr, cmd), "schedule UPSERT command");
    check(lcb_cmdstore_destroy(cmd), "destroy UPSERT command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    // end::expiration[]
}

static void
get_callback(lcb_INSTANCE*, int, const lcb_RESPGET* resp)
{
    lcb_STATUS status = lcb_respget_status(resp);
    if (status == LCB_SUCCESS) {
        const char* buf = nullptr;
        std::size_t buf_len = 0;
        check(lcb_respget_value(resp, &buf, &buf_len), "extract value from GET response");
        std::cout << "GET value: " << std::string(buf, buf_len) << "\n";
    } else {
        std::cout << "GET failed: " << lcb_strerror_short(status) << "\n";
    }
}

static void
get_key(lcb_INSTANCE* instance, const std::string& key, std::chrono::seconds expiry = {})
{
    lcb_CMDGET* cmd = nullptr;
    check(lcb_cmdget_create(&cmd), "create GET command");
    check(lcb_cmdget_key(cmd, key.c_str(), key.size()), "assign ID for GET command");
    check(lcb_cmdget_expiry(cmd, static_cast<std::uint32_t>(expiry.count())), "assign expiration to GET command");
    check(lcb_get(instance, nullptr, cmd), "schedule GET command");
    check(lcb_cmdget_destroy(cmd), "destroy GET command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
}

static void
touch_callback(lcb_INSTANCE*, int, const lcb_RESPTOUCH* resp)
{
    lcb_STATUS status = lcb_resptouch_status(resp);
    if (status == LCB_SUCCESS) {
        std::cout << "TOUCH succeed\n";
    } else {
        std::cout << "TOUCH failed: " << lcb_strerror_short(status) << "\n";
    }
}

static void
touch_key(lcb_INSTANCE* instance, const std::string& key, std::chrono::seconds expiry)
{
    lcb_CMDTOUCH* cmd = nullptr;
    check(lcb_cmdtouch_create(&cmd), "create TOUCH command");
    check(lcb_cmdtouch_key(cmd, key.c_str(), key.size()), "assign ID for TOUCH command");
    check(lcb_cmdtouch_expiry(cmd, static_cast<std::uint32_t>(expiry.count())), "assign expiration to TOUCH command");
    check(lcb_touch(instance, nullptr, cmd), "schedule TOUCH command");
    check(lcb_cmdtouch_destroy(cmd), "destroy TOUCH command");
    lcb_wait(instance, LCB_WAIT_DEFAULT);
}

int
main(int, char**)
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
    lcb_install_callback(instance, LCB_CALLBACK_STORE, reinterpret_cast<lcb_RESPCALLBACK>(store_callback));
    lcb_install_callback(instance, LCB_CALLBACK_TOUCH, reinterpret_cast<lcb_RESPCALLBACK>(touch_callback));

    std::string key = "docid";
    std::string value = R"({"some":"value"})";

    // First store with an expiry of 1 second
    std::cout << "--- Storing with an expiration of 2 seconds\n";
    store_key(instance, key, value, std::chrono::seconds(2));
    std::cout << "--- Getting key immediately after store..\n";
    get_key(instance, key);

    printf("--- Sleeping for 4 seconds..\n");
    std::this_thread::sleep_for(std::chrono::seconds(4));
    std::cout << "--- Getting key again (should fail)\n";
    get_key(instance, key);

    printf("--- Storing key again (without expiry)\n");
    store_key(instance, key, value);
    printf("--- Using get-and-touch to retrieve key and modify expiry (set to 1 second)\n");
    get_key(instance, key, std::chrono::seconds(1));

    printf("--- Sleeping for another 4 seconds\n");
    std::this_thread::sleep_for(std::chrono::seconds(4));
    printf("--- Getting key again (should fail)\n");
    get_key(instance, key);

    printf("--- Storing key again (without expiry)\n");
    store_key(instance, key, value);

    printf("--- Touching key (without get). Setting expiry for 1 second\n");
    touch_key(instance, key, std::chrono::seconds(1));

    printf("--- Sleeping for 4 seconds\n");
    std::this_thread::sleep_for(std::chrono::seconds(4));

    printf("--- Getting again... (should fail)\n");
    get_key(instance, key);

    lcb_destroy(instance);
}

// OUTPUT

// --- Storing with an expiration of 2 seconds
// STORE succeed
// --- Getting key immediately after store..
// GET value: {"some":"value"}
// --- Sleeping for 4 seconds..
// --- Getting key again (should fail)
// GET failed: LCB_ERR_DOCUMENT_NOT_FOUND (301)
// --- Storing key again (without expiry)
// STORE succeed
// --- Using get-and-touch to retrieve key and modify expiry (set to 1 second)
// GET value: {"some":"value"}
// --- Sleeping for another 4 seconds
// --- Getting key again (should fail)
// GET failed: LCB_ERR_DOCUMENT_NOT_FOUND (301)
// --- Storing key again (without expiry)
// STORE succeed
// --- Touching key (without get). Setting expiry for 1 second
// TOUCH succeed
// --- Sleeping for 4 seconds
// --- Getting again... (should fail)
// GET failed: LCB_ERR_DOCUMENT_NOT_FOUND (301)
