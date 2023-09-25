/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2023 MaNGOS <https://getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "EventProcessor.h"

EventProcessor::EventProcessor()
{
    m_time = 0;
    m_aborting = false;
}

EventProcessor::~EventProcessor()
{
    // 退出时强制清理所有Event
    KillAllEvents(true);
}

void EventProcessor::Update(uint32 p_time)
{
    // 更新时间
    m_time += p_time;

    // 循环队列，取出其中到期的Event
    EventList::iterator i;
    while (((i = m_events.begin()) != m_events.end()) && i->first <= m_time)
    {
        // 获取Event并从队列中删除
        BasicEvent* Event = i->second;
        m_events.erase(i);
        // 如果Event不需要终止
        if (!Event->to_Abort)
        {
            // 回调Execute函数后，删除Event
            if (Event->Execute(m_time, p_time))
            {
                // 如果回调Event的Execute函数返回true，删除该Event
                delete Event;
            }
        }
        else
        {
            // 回调Abort函数后删除Event
            Event->Abort(m_time);
            delete Event;
        }
    }
}

void EventProcessor::KillAllEvents(bool force)
{
    // 阻止插入Event
    m_aborting = true;

    // 终止队列中的Event
    for (EventList::iterator i = m_events.begin(); i != m_events.end();)
    {
        EventList::iterator i_old = i;
        ++i;
        // 首先终止Event，并回调to_Abort函数
        i_old->second->to_Abort = true;
        i_old->second->Abort(m_time);
        // 如果使用了强制结束或者Event可以被删除
        if (force || i_old->second->IsDeletable())
        {
            delete i_old->second;

            if (!force)                                     // need per-element cleanup
            {
                m_events.erase(i_old);
            }
        }
    }

    // fast clear event list (in force case)
    if (force)
    {
        m_events.clear();
    }
}

void EventProcessor::AddEvent(BasicEvent* Event, uint64 e_time, bool set_addtime)
{
    if (set_addtime)
    {
        Event->m_addTime = m_time;
    }

    Event->m_execTime = e_time;
    m_events.insert(std::pair<uint64, BasicEvent*>(e_time, Event));
}

uint64 EventProcessor::CalculateTime(uint64 t_offset)
{
    return m_time + t_offset;
}
