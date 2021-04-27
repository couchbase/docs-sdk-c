#include <string>
#include <iostream>

#include <libcouchbase/couchbase.h>

static void die(const char *msg, lcb_STATUS err)
{
    std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
    exit(EXIT_FAILURE);
}

static void store_callback(lcb_INSTANCE *, int cbtype, const lcb_RESPSTORE *resp)
{
    lcb_STATUS rc = lcb_respstore_status(resp);
    std::cerr << "=== " << lcb_strcbtype(cbtype) << " ===\n";
    if (rc == LCB_SUCCESS) {
        const char *key;
        size_t nkey;
        uint64_t cas;
        lcb_respstore_key(resp, &key, &nkey);
        std::cerr << "KEY: " << std::string(key, nkey) << "\n";
        lcb_respstore_cas(resp, &cas);
        std::cerr << "CAS: 0x" << std::hex << cas << "\n";
    } else {
        die(lcb_strcbtype(cbtype), rc);
    }
}

// tag::retrieve[]
static void get_callback(lcb_INSTANCE *instance, int cbtype, const lcb_RESPGET *resp)
{
    lcb_STATUS rc = lcb_respget_status(resp);
    std::cerr << "=== " << lcb_strcbtype(cbtype) << " ===\n";
    if (rc == LCB_SUCCESS) {
        const char *key, *value;
        size_t nkey, nvalue;
        uint64_t cas;
        uint32_t flags;
        lcb_respget_key(resp, &key, &nkey);
        std::cerr << "KEY: " << std::string(key, nkey) << "\n";
        lcb_respget_cas(resp, &cas);
        std::cerr << "CAS: 0x" << std::hex << cas << "\n";
        lcb_respget_value(resp, &value, &nvalue);
        std::cerr << "VALUE: " << std::string(value, nvalue) << "\n";
        lcb_respget_flags(resp, &flags);
        std::cerr << "FLAGS: 0x" << std::hex << flags << "\n";

        {
            // tag::async[]
            // This snippet lives inside the callback, so it is not necessary to call lcb_wait here
            lcb_CMDSTORE *cmd;
            lcb_cmdstore_create(&cmd, LCB_STORE_INSERT);
            lcb_cmdstore_key(cmd, key, nkey);
            lcb_cmdstore_value(cmd, value, nvalue);

            lcb_STATUS err = lcb_store(instance, nullptr, cmd);
            lcb_cmdstore_destroy(cmd);
            if (err != LCB_SUCCESS) {
                die("Couldn't schedule storage operation", err);
            }
            // end::async[]
        }
    } else {
        die(lcb_strcbtype(cbtype), rc);
    }
}
// end::retrieve[]

int main(int, char **)
{
    lcb_STATUS err;
    lcb_INSTANCE *instance;
    lcb_CREATEOPTS *create_options = nullptr;
    lcb_CMDSTORE *scmd;
    lcb_CMDGET *gcmd;

    std::string connection_string = "couchbase://localhost";
    std::string username = "Administrator";
    std::string password = "password";

    lcb_createopts_create(&create_options, LCB_TYPE_BUCKET);
    lcb_createopts_connstr(create_options, connection_string.data(), connection_string.size());
    lcb_createopts_credentials(create_options, username.data(), username.size(), password.data(), password.size());

    err = lcb_create(&instance, create_options);
    lcb_createopts_destroy(create_options);
    if (err != LCB_SUCCESS) {
        die("Couldn't create couchbase handle", err);
    }

    err = lcb_connect(instance);
    if (err != LCB_SUCCESS) {
        die("Couldn't schedule connection", err);
    }

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    err = lcb_get_bootstrap_status(instance);
    if (err != LCB_SUCCESS) {
        die("Couldn't bootstrap from cluster", err);
    }

    /* Assign the handlers to be called for the operation types */
    lcb_install_callback(instance, LCB_CALLBACK_GET, reinterpret_cast<lcb_RESPCALLBACK>(get_callback));
    lcb_install_callback(instance, LCB_CALLBACK_STORE, reinterpret_cast<lcb_RESPCALLBACK>(store_callback));

    std::string key("key");
    std::string value("value");

    {
        // tag::upsert[]
        lcb_cmdstore_create(&scmd, LCB_STORE_UPSERT);
        lcb_cmdstore_key(scmd, key.data(), key.size());
        lcb_cmdstore_value(scmd, value.data(), value.size());

        err = lcb_store(instance, nullptr, scmd);
        lcb_cmdstore_destroy(scmd);
        if (err != LCB_SUCCESS) {
            die("Couldn't schedule storage operation", err);
        }
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        // end::upsert[]
    }

    {
        // tag::insert[]
        lcb_cmdstore_create(&scmd, LCB_STORE_INSERT);
        lcb_cmdstore_key(scmd, key.data(), key.size());
        lcb_cmdstore_value(scmd, value.data(), value.size());

        err = lcb_store(instance, nullptr, scmd);
        lcb_cmdstore_destroy(scmd);
        if (err != LCB_SUCCESS) {
            die("Couldn't schedule storage operation", err);
        }
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        // end::insert[]
    }

    /* Now fetch the item back */
    lcb_cmdget_create(&gcmd);
    lcb_cmdget_key(gcmd, key.data(), key.size());
    err = lcb_get(instance, nullptr, gcmd);
    if (err != LCB_SUCCESS) {
        die("Couldn't schedule retrieval operation", err);
    }
    lcb_cmdget_destroy(gcmd);

    /* Likewise, the get_callback is invoked from here */
    std::cerr << "Will wait to retrieve item..\n";
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    /* Now that we're all done, close down the connection handle */
    lcb_destroy(instance);
    return 0;
}