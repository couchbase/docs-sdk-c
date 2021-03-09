#include <stdio.h>
#include <libcouchbase/couchbase.h>
#include <stdlib.h>
#include <string.h> /* strlen */
#ifdef _WIN32
#define PRIx64 "I64x"
#else
#include <inttypes.h>
#endif

static void die(lcb_INSTANCE *instance, const char *msg, lcb_STATUS err)
{
    fprintf(stderr, "%s. Received code 0x%X (%s)\n", msg, err, lcb_strerror_short(err));
    exit(EXIT_FAILURE);
}

static void store_callback(lcb_INSTANCE *instance, int cbtype, const lcb_RESPSTORE *resp)
{
    lcb_STATUS rc = lcb_respstore_status(resp);
    fprintf(stderr, "=== %s ===\n", lcb_strcbtype(cbtype));
    if (rc == LCB_SUCCESS) {
        const char *key;
        size_t nkey;
        uint64_t cas;
        lcb_respstore_key(resp, &key, &nkey);
        fprintf(stderr, "KEY: %.*s\n", (int)nkey, key);
        lcb_respstore_cas(resp, &cas);
        fprintf(stderr, "CAS: 0x%" PRIx64 "\n", cas);
    } else {
        die(instance, lcb_strcbtype(cbtype), rc);
    }
}

// tag::retrieve[]
static void get_callback(lcb_INSTANCE *instance, int cbtype, const lcb_RESPGET *resp)
{
    lcb_STATUS rc = lcb_respget_status(resp);
    fprintf(stderr, "=== %s ===\n", lcb_strcbtype(cbtype));
    if (rc == LCB_SUCCESS) {
        const char *key, *value;
        size_t nkey, nvalue;
        uint64_t cas;
        uint32_t flags;
        lcb_respget_key(resp, &key, &nkey);
        fprintf(stderr, "KEY: %.*s\n", (int)nkey, key);
        lcb_respget_cas(resp, &cas);
        fprintf(stderr, "CAS: 0x%" PRIx64 "\n", cas);
        lcb_respget_value(resp, &value, &nvalue);
        lcb_respget_flags(resp, &flags);
        fprintf(stderr, "VALUE: %.*s\n", (int)nvalue, value);
        fprintf(stderr, "FLAGS: 0x%x\n", flags);
    } else {
        die(instance, lcb_strcbtype(cbtype), rc);
    }
}
// end::retrieve[]

int main(int argc, char *argv[])
{
    lcb_STATUS err;
    lcb_INSTANCE *instance;
    lcb_CREATEOPTS *create_options = NULL;
    lcb_CMDSTORE *scmd;
    lcb_CMDGET *gcmd;

    char connection_string[] = "couchbase://localhost";
    char username = "administrator";
    char password = "password";

    lcb_createopts_create(&create_options, LCB_TYPE_BUCKET);
    lcb_createopts_connstr(create_options, connection_string, strlen(connection_string));
    lcb_createopts_credentials(create_options, username, strlen(username), password, strlen(password));

    err = lcb_create(&instance, create_options);
    lcb_createopts_destroy(create_options);
    if (err != LCB_SUCCESS) {
        die(NULL, "Couldn't create couchbase handle", err);
    }

    err = lcb_connect(instance);
    if (err != LCB_SUCCESS) {
        die(instance, "Couldn't schedule connection", err);
    }

    lcb_wait(instance, LCB_WAIT_DEFAULT);

    err = lcb_get_bootstrap_status(instance);
    if (err != LCB_SUCCESS) {
        die(instance, "Couldn't bootstrap from cluster", err);
    }

    /* Assign the handlers to be called for the operation types */
    lcb_install_callback(instance, LCB_CALLBACK_GET, (lcb_RESPCALLBACK)get_callback);
    lcb_install_callback(instance, LCB_CALLBACK_STORE, (lcb_RESPCALLBACK)store_callback);

    // tag::upsert[]
    lcb_cmdstore_create(&scmd, LCB_STORE_UPSERT);
    lcb_cmdstore_key(scmd, "key", strlen("key"));
    lcb_cmdstore_value(scmd, "value", strlen("value"));

    err = lcb_store(instance, NULL, scmd);
    lcb_cmdstore_destroy(scmd);
    if (err != LCB_SUCCESS) {
        die(instance, "Couldn't schedule storage operation", err);
    }
    // end::upsert[]


    // tag::insert[]
    lcb_cmdstore_create(&scmd, LCB_STORE_INSERT);
    lcb_cmdstore_key(scmd, "key", strlen("key"));
    lcb_cmdstore_value(scmd, "value", strlen("value"));

    err = lcb_store(instance, NULL, scmd);
    lcb_cmdstore_destroy(scmd);
    if (err != LCB_SUCCESS) {
        die(instance, "Couldn't schedule storage operation", err);
    }
    // end::insert[]


    // tag::async[]
    lcb_cmdstore_create(&scmd, LCB_STORE_INSERT);
    lcb_cmdstore_key(scmd, "key", strlen("key"));
    lcb_cmdstore_value(scmd, "value", strlen("value"));
    lcb_wait(instance, LCB_WAIT_DEFAULT);
    // end::async[]

    /* Now fetch the item back */
    lcb_cmdget_create(&gcmd);
    lcb_cmdget_key(gcmd, "key", strlen("key"));
    err = lcb_get(instance, NULL, gcmd);
    if (err != LCB_SUCCESS) {
        die(instance, "Couldn't schedule retrieval operation", err);
    }
    lcb_cmdget_destroy(gcmd);

    /* Likewise, the get_callback is invoked from here */
    fprintf(stderr, "Will wait to retrieve item..\n");
    lcb_wait(instance, LCB_WAIT_DEFAULT);

    /* Now that we're all done, close down the connection handle */
    lcb_destroy(instance);
    return 0;
