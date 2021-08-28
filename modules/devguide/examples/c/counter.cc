#include <libcouchbase/couchbase.h>
#include <string>
#include <iostream>

static void
check(lcb_STATUS err, const char *msg) {
    if (err != LCB_SUCCESS) {
        std::cerr << "[ERROR] " << msg << ": " << lcb_strerror_short(err) << "\n";
        exit(EXIT_FAILURE);
    }
}

static void
counter_callback(lcb_INSTANCE *, int, const lcb_RESPCOUNTER *resp) {
    check(lcb_respcounter_status(resp), "perform COUNTER operation (callback)");
    std::uint64_t value;
    check(lcb_respcounter_value(resp, &value), "extract current value from COUNTER result");
    std::cout << "Current counter value is " << value << "\n";
}

int
main() {
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

    lcb_install_callback(instance, LCB_CALLBACK_COUNTER,
                         reinterpret_cast<lcb_RESPCALLBACK>(counter_callback));

    std::string document_id{"counter_id"};

    {
        // remove counter document
        lcb_CMDREMOVE *cmd = nullptr;
        check(lcb_cmdremove_create(&cmd), "create REMOVE command");
        check(lcb_cmdremove_key(cmd, document_id.data(), document_id.size()),
              "assign ID for REMOVE command");
        check(lcb_remove(instance, nullptr, cmd), "schedule REMOVE command");
        check(lcb_cmdremove_destroy(cmd), "destroy REMOVE command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    {
        // increment counter by 20 if it exists, and initialize with 100 otherwise
        lcb_CMDCOUNTER *cmd = nullptr;
        // tag::atomic-counter[]
        check(lcb_cmdcounter_create(&cmd), "create COUNTER command");
        check(lcb_cmdcounter_key(cmd, document_id.data(), document_id.size()),
              "assign ID for COUNTER command");
        check(lcb_cmdcounter_initial(cmd, 100), "assign initial value for COUNTER command");
        check(lcb_cmdcounter_delta(cmd, 20), "assign delta value for COUNTER command");
        check(lcb_counter(instance, nullptr, cmd), "schedule COUNTER command");
        check(lcb_cmdcounter_destroy(cmd), "destroy COUNTER command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
        // end::atomic-counter[]
    }

    {
        // increment counter by 1
        lcb_CMDCOUNTER *cmd = nullptr;
        check(lcb_cmdcounter_create(&cmd), "create COUNTER command");
        check(lcb_cmdcounter_key(cmd, document_id.data(), document_id.size()),
              "assign ID for COUNTER command");
        check(lcb_cmdcounter_delta(cmd, 1), "assign delta value for COUNTER command");
        check(lcb_counter(instance, nullptr, cmd), "schedule COUNTER command");
        check(lcb_cmdcounter_destroy(cmd), "destroy COUNTER command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    {
        // decrement counter by 50
        lcb_CMDCOUNTER *cmd = nullptr;
        check(lcb_cmdcounter_create(&cmd), "create COUNTER command");
        check(lcb_cmdcounter_key(cmd, document_id.data(), document_id.size()),
              "assign ID for COUNTER command");
        check(lcb_cmdcounter_delta(cmd, -50), "assign delta value for COUNTER command");
        check(lcb_counter(instance, nullptr, cmd), "schedule COUNTER command");
        check(lcb_cmdcounter_destroy(cmd), "destroy COUNTER command");
        lcb_wait(instance, LCB_WAIT_DEFAULT);
    }

    lcb_destroy(instance);
}

// OUTPUT
//
// Current counter value is 100
// Current counter value is 101
// Current counter value is 51
