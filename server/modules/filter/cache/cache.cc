/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cache.hh"
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include "storagefactory.hh"
#include "storage.hh"

Cache::Cache(const std::string&  name,
             const CACHE_CONFIG* pConfig,
             SCacheRules         sRules,
             SStorageFactory     sFactory)
    : m_name(name)
    , m_config(*pConfig)
    , m_sRules(sRules)
    , m_sFactory(sFactory)
{
}

Cache::~Cache()
{
}

//static
bool Cache::Create(const CACHE_CONFIG& config,
                   CacheRules**        ppRules,
                   StorageFactory**    ppFactory)
{
    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (config.rules)
    {
        pRules = CacheRules::load(config.rules, config.debug);
    }
    else
    {
        pRules = CacheRules::create(config.debug);
    }

    if (pRules)
    {
        pFactory = StorageFactory::Open(config.storage);

        if (pFactory)
        {
            *ppFactory = pFactory;
            *ppRules = pRules;
        }
        else
        {
            MXS_ERROR("Could not open storage factory '%s'.", config.storage);
            delete pRules;
        }
    }
    else
    {
        MXS_ERROR("Could not create rules.");
    }

    return pFactory != NULL;
}

void Cache::show(DCB* pDcb) const
{
    bool showed = false;
    json_t* pInfo = get_info(INFO_ALL);

    if (pInfo)
    {
        size_t flags = JSON_PRESERVE_ORDER;
        size_t indent = 2;
        char* z = json_dumps(pInfo, JSON_PRESERVE_ORDER | JSON_INDENT(indent));

        if (z)
        {
            dcb_printf(pDcb, "%s\n", z);
            free(z);
            showed = true;
        }

        json_decref(pInfo);
    }

    if (!showed)
    {
        // So as not to upset anyone expecting a JSON object.
        dcb_printf(pDcb, "{\n}\n");
    }
}

bool Cache::should_store(const char* zDefaultDb, const GWBUF* pQuery)
{
    return m_sRules->should_store(zDefaultDb, pQuery);
}

bool Cache::should_use(const SESSION* pSession)
{
    return m_sRules->should_use(pSession);
}

json_t* Cache::do_get_info(uint32_t what) const
{
    json_t* pInfo = json_object();

    if (pInfo)
    {
        if (what & INFO_RULES)
        {
            json_t* pRules = const_cast<json_t*>(m_sRules->json());

            json_object_set(pInfo, "rules", pRules); // Increases ref-count of pRules, we ignore failure.
        }
    }

    return pInfo;
}