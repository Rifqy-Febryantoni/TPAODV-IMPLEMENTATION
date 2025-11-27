/*
 * Copyright (c) 2009 IITP RAS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Based on
 *      NS-2 PAODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      PAODV-UU implementation by Erik Nordström of Uppsala University
 *      https://web.archive.org/web/20100527072022/http://core.it.uu.se/core/index.php/PAODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#include "paodv-id-cache.h"

#include <algorithm>

namespace ns3
{
namespace paodv
{
bool
IdCache::IsDuplicate(Ipv4Address addr, uint32_t id)
{
    Purge();
    for (auto i = m_idCache.begin(); i != m_idCache.end(); ++i)
    {
        if (i->m_context == addr && i->m_id == id)
        {
            return true;
        }
    }
    UniqueId uniqueId = {addr, id, m_lifetime + Simulator::Now()};
    m_idCache.push_back(uniqueId);
    return false;
}

void
IdCache::Purge()
{
    m_idCache.erase(remove_if(m_idCache.begin(), m_idCache.end(), IsExpired()), m_idCache.end());
}

uint32_t
IdCache::GetSize()
{
    Purge();
    return m_idCache.size();
}

} // namespace paodv
} // namespace ns3
