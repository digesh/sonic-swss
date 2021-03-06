#include <unistd.h>
#include <getopt.h>
#include <vector>
#include <mutex>
#include "dbconnector.h"
#include "select.h"
#include "exec.h"
#include "schema.h"
#include "buffermgr.h"
#include <fstream>
#include <iostream>

using namespace std;
using namespace swss;

/* select() function timeout retry time, in millisecond */
#define SELECT_TIMEOUT 1000

/*
 * Following global variables are defined here for the purpose of
 * using existing Orch class which is to be refactored soon to
 * eliminate the direct exposure of the global variables.
 *
 * Once Orch class refactoring is done, these global variables
 * should be removed from here.
 */
int gBatchSize = 0;
bool gSwssRecord = false;
bool gLogRotate = false;
ofstream gRecordOfs;
string gRecordFile;
/* Global database mutex */
mutex gDbMutex;

void usage()
{
    cout << "Usage: buffermgrd -l pg_lookup.ini" << endl;
    cout << "       -l pg_lookup.ini: PG profile look up table file (mandatory)" << endl;
    cout << "       format: csv" << endl;
    cout << "       values: 'speed, cable, size, xon,  xoff, dynamic_threshold'" << endl;
}

int main(int argc, char **argv)
{
    int opt;
    string pg_lookup_file = "";
    Logger::linkToDbNative("buffermgrd");
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("--- Starting buffermgrd ---");

    while ((opt = getopt(argc, argv, "l:h")) != -1 )
    {
        switch (opt)
        {
        case 'l':
            pg_lookup_file = optarg;
            break;
        case 'h':
            usage();
            return 1;
        default: /* '?' */
            usage();
            return EXIT_FAILURE;
        }
    }

    if (pg_lookup_file.empty())
    {
        usage();
        return EXIT_FAILURE;
    }

    try
    {
        vector<string> cfg_buffer_tables = {
            CFG_PORT_TABLE_NAME,
            CFG_PORT_CABLE_LEN_TABLE_NAME,
        };

        DBConnector cfgDb(CONFIG_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);
        DBConnector stateDb(STATE_DB, DBConnector::DEFAULT_UNIXSOCKET, 0);

        BufferMgr buffmgr(&cfgDb, &stateDb, pg_lookup_file, cfg_buffer_tables);

        // TODO: add tables in stateDB which interface depends on to monitor list
        std::vector<Orch *> cfgOrchList = {&buffmgr};

        swss::Select s;
        for (Orch *o : cfgOrchList)
        {
            s.addSelectables(o->getSelectables());
        }

        SWSS_LOG_NOTICE("starting main loop");
        while (true)
        {
            Selectable *sel;
            int fd, ret;

            ret = s.select(&sel, &fd, SELECT_TIMEOUT);
            if (ret == Select::ERROR)
            {
                SWSS_LOG_NOTICE("Error: %s!", strerror(errno));
                continue;
            }
            if (ret == Select::TIMEOUT)
            {
                buffmgr.doTask();
                continue;
            }

            auto *c = (Executor *)sel;
            c->execute();
        }
    }
    catch(const std::exception &e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
    return -1;
}
