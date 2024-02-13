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

#ifndef MANGOS_H_EVENTPROCESSOR
#define MANGOS_H_EVENTPROCESSOR

#include "Platform/Define.h"

#include <map>

/**
 * @brief 所有时间单位为毫秒
 *
 */
class BasicEvent
{
    public:

        /**
         * @brief
         *
         */
        BasicEvent()
            : to_Abort(false)
        {
        }

        /**
         * @brief override destructor to perform some actions on event removal
         *
         */
        virtual ~BasicEvent()
        {
        };


        /**
         * @brief 这个函数在Event被触发的时候执行
         *
         * @param uint64 e_time 开始执行时间
         * @param uint32 p_time 更新间隔
         * @return bool 如果不想要删除Event，返回false
         */
        virtual bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) { return true; }

        /**
         * @brief Event能否被安全的删除
         *
         * @return bool
         */
        virtual bool IsDeletable() const { return true; }

        /**
         * @brief 这个函数在Event终止时执行
         *
         * @param uint64
         */
        virtual void Abort(uint64 /*e_time*/) {}

        /**
         * @brief 是否终止Event，终止时调用Abort函数
        */
        bool to_Abort;

        /**
         * @brief Event加入处理队列的时间
        */
        uint64 m_addTime;
        /**
         * @brief Event触发时间
        */
        uint64 m_execTime;
};

/**
 * @brief
 *
 */
typedef std::multimap<uint64, BasicEvent*> EventList;

/**
 * @brief
 *
 */
class EventProcessor
{
    public:

        /**
         * @brief
         *
         */
        EventProcessor();
        /**
         * @brief
         *
         */
        ~EventProcessor();

        /**
         * @brief 更新EventProcessor的时间
         * @param p_time 增加的时间 
        */
        void Update(uint32 p_time); 
        /**
         * @brief 关闭所有事件
         * @param 是否强制关闭
        */
        void KillAllEvents(bool force);
        /**
         * @brief 添加Event到处理队列
         * @param Event 要处理的Event
         * @param e_time Event触发时间
         * @param set_addtime 是否设置Event加入时间
        */
        void AddEvent(BasicEvent* Event, uint64 e_time, bool set_addtime = true);
        /**
         * @brief 计算时间
         * @param t_offset 要增加的时间
         * @return 当前时间+增加的时间
        */
        uint64 CalculateTime(uint64 t_offset);

    protected:

        /**
         * @brief 当前时间
        */
        uint64 m_time;
        /**
         * @brief Event处理队列
        */
        EventList m_events;
        /**
         * @brief 是否阻止Event插入
        */
        bool m_aborting;
};

#endif
