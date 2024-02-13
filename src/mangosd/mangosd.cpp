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

 /// \addtogroup mangosd Mangos Daemon
 /// @{
 /// \file

#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <ace/Version.h>
#include <ace/Get_Opt.h>

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "GitRevision.h"
#include "ProgressBar.h"
#include "Log.h"
#include "SystemConfig.h"
#include "AuctionHouseBot.h"
#include "revision_data.h"
#include "World.h"
#include "Util.h"
#include "DBCStores.h"
#include "MassMailMgr.h"
#include "ScriptMgr.h"

#include "WorldThread.h"
#include "CliThread.h"
#include "AFThread.h"
#include "RAThread.h"

#ifdef ENABLE_SOAP
#include "SOAP/SoapThread.h"
#endif

#ifdef _WIN32
#include "ServiceWin32.h"

char serviceName[] = "MaNGOS";               // service short name
char serviceLongName[] = "MaNGOS World Service"; // service long name
char serviceDescription[] = "MaNGOS World Service - no description available";

int m_ServiceStatus = -1;

#else
#include "PosixDaemon.h"
#endif

//*******************************************************************************************************//
DatabaseType WorldDatabase;                                 ///< Accessor to the world database
DatabaseType CharacterDatabase;                             ///< Accessor to the character database
DatabaseType LoginDatabase;                                 ///< Accessor to the realm/login database

uint32 realmID = 0;                                         ///< Id of the realm
//*******************************************************************************************************//


/// Clear 'online' status for all accounts with characters in this realm
static void clear_online_accounts()
{
	// Cleanup online status for characters hosted at current realm
	/// \todo Only accounts with characters logged on *this* realm should have online status reset. Move the online column from 'account' to 'realmcharacters'?
	LoginDatabase.PExecute("UPDATE `account` SET `active_realm_id` = 0, `os` = ''  WHERE `active_realm_id` = '%u'", realmID);

	CharacterDatabase.Execute("UPDATE `characters` SET `online` = 0 WHERE `online`<>0");

	// Battleground instance ids reset at server restart
	CharacterDatabase.Execute("UPDATE `character_battleground_data` SET `instance_id` = 0");
}


/**
 * @brief 初始化数据库连接
 * @return 
*/
static bool start_db()
{
	// 从配置文件获取WorldDatabase配置信息
	std::string dbstring = sConfig.GetStringDefault("WorldDatabaseInfo", "");
	// 从配置文件获取WorldDatabase连接数量配置信息
	int nConnections = sConfig.GetIntDefault("WorldDatabaseConnections", 1);
	// 获取配置信息为空，返回false
	if (dbstring.empty())
	{
		sLog.outError("Database not specified in configuration file");
		return false;
	}
	sLog.outString("World Database total connections: %i", nConnections + 1);

	// 初始化WorldDatabase数据库
	if (!WorldDatabase.Initialize(dbstring.c_str(), nConnections))
	{
		sLog.outError("Can not connect to world database %s", dbstring.c_str());
		return false;
	}

	// 检查World database版本
	if (!WorldDatabase.CheckDatabaseVersion(DATABASE_WORLD))
	{
		// 等待已经启动的db延迟线程结束
		WorldDatabase.HaltDelayThread();
		return false;
	}
	// 从配置文件获取Character Database信息
	dbstring = sConfig.GetStringDefault("CharacterDatabaseInfo", "");
	nConnections = sConfig.GetIntDefault("CharacterDatabaseConnections", 1);
	if (dbstring.empty())
	{
		sLog.outError("Character Database not specified in configuration file");

		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		return false;
	}
	sLog.outString("Character Database total connections: %i", nConnections + 1);

	// 初始化Character Database
	if (!CharacterDatabase.Initialize(dbstring.c_str(), nConnections))
	{
		sLog.outError("Can not connect to Character database %s", dbstring.c_str());

		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		return false;
	}

	// 检查Character Database版本信息
	if (!CharacterDatabase.CheckDatabaseVersion(DATABASE_CHARACTER))
	{
		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		CharacterDatabase.HaltDelayThread();
		return false;
	}

	// 从配置文件获取Realm database信息
	dbstring = sConfig.GetStringDefault("LoginDatabaseInfo", "");
	nConnections = sConfig.GetIntDefault("LoginDatabaseConnections", 1);
	if (dbstring.empty())
	{
		sLog.outError("Login database not specified in configuration file");

		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		CharacterDatabase.HaltDelayThread();
		return false;
	}

	// 初始化Realm database
	sLog.outString("Login Database total connections: %i", nConnections + 1);
	if (!LoginDatabase.Initialize(dbstring.c_str(), nConnections))
	{
		sLog.outError("Can not connect to login database %s", dbstring.c_str());

		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		CharacterDatabase.HaltDelayThread();
		return false;
	}

	// 检查Realm database版本信息
	if (!LoginDatabase.CheckDatabaseVersion(DATABASE_REALMD))
	{
		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		CharacterDatabase.HaltDelayThread();
		LoginDatabase.HaltDelayThread();
		return false;
	}

	sLog.outString();

	// 从配置文件获取RealmID
	realmID = sConfig.GetIntDefault("RealmID", 0);
	if (!realmID)
	{
		sLog.outError("Realm ID not defined in configuration file");

		///- Wait for already started DB delay threads to end
		WorldDatabase.HaltDelayThread();
		CharacterDatabase.HaltDelayThread();
		LoginDatabase.HaltDelayThread();
		return false;
	}

	sLog.outString("Realm running as realm ID %d", realmID);
	sLog.outString();

	// 在启动之前清理数据库
	clear_online_accounts();

	sWorld.LoadDBVersion();

	sLog.outString("Using World DB: %s", sWorld.GetDBVersion());
	sLog.outString();
	return true;
}

/// Handle termination signals
static void on_signal(int s)
{
	switch (s)
	{
	case SIGINT:
		World::StopNow(RESTART_EXIT_CODE);
		break;
	case SIGTERM:
#ifdef _WIN32
	case SIGBREAK:
#endif
		World::StopNow(SHUTDOWN_EXIT_CODE);
		break;
	}

	signal(s, on_signal);
}

/// Define hook for all termination signals
static void hook_signals()
{
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
#ifdef _WIN32
	signal(SIGBREAK, on_signal);
#endif
}

/// Unhook the signals before leaving
static void unhook_signals()
{
	signal(SIGINT, 0);
	signal(SIGTERM, 0);
#ifdef _WIN32
	signal(SIGBREAK, 0);
#endif
}


/// Print out the usage string for this program on the console.
static void usage(const char* prog)
{
	sLog.outString("Usage: \n %s [<options>]\n"
		"    -v, --version              print version and exist\n\r"
		"    -c <config_file>           use config_file as configuration file\n\r"
		"    -a, --ahbot <config_file>  use config_file as ahbot configuration file\n\r"
#ifdef WIN32
		"    Running as service functions:\n\r"
		"    -s run                     run as service\n\r"
		"    -s install                 install service\n\r"
		"    -s uninstall               uninstall service\n\r"
#else
		"    Running as daemon functions:\n\r"
		"    -s run                     run as daemon\n\r"
		"    -s stop                    stop daemon\n\r"
#endif
		, prog);
}

/**
 * @brief 启动Mangos服务器
 * @param argc 参数数目
 * @param argv 参数值
 * @return 
*/
int main(int argc, char** argv)
{
	// 默认配置文件地址
	char const* cfg_file = MANGOSD_CONFIG_LOCATION;

	char const* options = ":a:c:s:";
	// 创建解析命令行参数工具
	ACE_Get_Opt cmd_opts(argc, argv, options);
	// 设置命令行选项
	cmd_opts.long_option("version", 'v', ACE_Get_Opt::NO_ARG);
	cmd_opts.long_option("ahbot", 'a', ACE_Get_Opt::ARG_REQUIRED);

	char serviceDaemonMode = '\0';
	// 遍历命令行参数
	int option;
	while ((option = cmd_opts()) != EOF)
	{
		switch (option)
		{
		case 'a': {
			// 设置AHbot配置文件路径
			sAuctionBotConfig.SetConfigFileName(cmd_opts.opt_arg());
			break;
		}
		case 'c': {
			// 设置配置文件路径
			cfg_file = cmd_opts.opt_arg();
			break;
		}
		case 'v':{
			// 输出版本信息
			printf("%s\n", GitRevision::GetProjectRevision());
			return 0;
		}
		case 's':
		{
			// 获取服务模式信息
			const char* mode = cmd_opts.opt_arg();
			// 设置运行服务
			if (!strcmp(mode, "run"))
			{
				serviceDaemonMode = 'r';
			}
#ifdef WIN32
			else if (!strcmp(mode, "install"))
			{
				// 设置安装服务
				serviceDaemonMode = 'i';
			}
			else if (!strcmp(mode, "uninstall"))
			{
				// 设置卸载服务
				serviceDaemonMode = 'u';
			}
#else
			else if (!strcmp(mode, "stop"))
			{
				// 设置停止服务
				serviceDaemonMode = 's';
			}
#endif
			else
			{
				sLog.outError("Runtime-Error: -%c unsupported argument %s", cmd_opts.opt_opt(), mode);
				usage(argv[0]);
				Log::WaitBeforeContinueIfNeed();
				return 1;
			}
			break;
		}
		case ':':
			sLog.outError("Runtime-Error: -%c option requires an input argument", cmd_opts.opt_opt());
			usage(argv[0]);
			Log::WaitBeforeContinueIfNeed();
			return 1;
		default:
			sLog.outError("Runtime-Error: bad format of commandline arguments");
			usage(argv[0]);
			Log::WaitBeforeContinueIfNeed();
			return 1;
		}
	}

#ifdef _WIN32                                                // windows service command need execute before config read
	switch (serviceDaemonMode)
	{
	case 'i':
		if (WinServiceInstall())
		{
			sLog.outString("Installing service");
		}
		return 1;
	case 'u':
		if (WinServiceUninstall())
		{
			sLog.outString("Uninstalling service");
		}
		return 1;
	case 'r':
		WinServiceRun();
		break;
	}
#endif
	if (!sConfig.SetSource(cfg_file))
	{
		sLog.outError("Could not find configuration file %s.", cfg_file);
		Log::WaitBeforeContinueIfNeed();
		return 1;
	}

#ifndef _WIN32                                               // posix daemon commands need apply after config read
	switch (serviceDaemonMode)
	{
	case 'r':
		startDaemon();
		break;
	case 's':
		stopDaemon();
		break;
}
#endif
	// 输出Git版本信息
	sLog.outString("%s [world-daemon]", GitRevision::GetProjectRevision());
	sLog.outString("%s", GitRevision::GetFullRevision());
	// 输出banner
	print_banner();
	// 输出使用的配置文件
	sLog.outString("Using configuration file %s.", cfg_file);
	// 输出使用的SSL版本信息
	DETAIL_LOG("Using SSL version: %s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
	// SSL版本校验
	if (SSLeay() < 0x10100000L || SSLeay() > 0x10200000L)
	{
		DETAIL_LOG("WARNING: OpenSSL version may be out of date or unsupported. Logins to server may not work!");
		DETAIL_LOG("WARNING: Minimal required version [OpenSSL 1.1.x] and Maximum supported version [OpenSSL 1.2]");
	}
	// 输出ACE版本信息
	DETAIL_LOG("Using ACE: %s", ACE_VERSION);

	// 设置进度条模式
	BarGoLink::SetOutputState(sConfig.GetBoolDefault("ShowProgressBars", true));

	// 获取PID文件配置信息
	std::string pidfile = sConfig.GetStringDefault("PidFile", "");
	// 创建PID文件（文件内容为进程ID）
	if (!pidfile.empty())
	{
		uint32 pid = CreatePIDFile(pidfile);
		if (!pid)
		{
			sLog.outError("Can not create PID file %s.\n", pidfile.c_str());
			Log::WaitBeforeContinueIfNeed();
			return 1;
		}

		sLog.outString("Daemon PID: %u\n", pid);
	}

	// 启动数据库
	if (!start_db())
	{
		Log::WaitBeforeContinueIfNeed();
		return 1;
	}

	///- Set Realm to Offline, if crash happens. Only used once.
	LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = `realmflags` | %u WHERE `id` = '%u'", REALM_FLAG_OFFLINE, realmID);

	// 初始化世界
	sWorld.SetInitialWorldSettings();

#ifndef _WIN32
	detachDaemon();
#endif

	// set realmbuilds depend on mangosd expected builds, and set server online
	std::string builds = AcceptableClientBuildsListStr();
	LoginDatabase.escape_string(builds);
	LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = `realmflags` & ~(%u), `population` = 0, `realmbuilds` = '%s'  WHERE `id` = '%u'", REALM_FLAG_OFFLINE, builds.c_str(), realmID);

	// server loaded successfully => enable async DB requests
	// this is done to forbid any async transactions during server startup!

	WorldDatabase.ThreadStart();

	CharacterDatabase.AllowAsyncTransactions();
	WorldDatabase.AllowAsyncTransactions();
	LoginDatabase.AllowAsyncTransactions();

	///- Catch termination signals
	hook_signals();


	//************************************************************************************************************************
	// 1. Start the World thread
	//************************************************************************************************************************

	std::string host = sConfig.GetStringDefault("BindIP", "0.0.0.0");
	uint16 port = sWorld.getConfig(CONFIG_UINT32_PORT_WORLD);

	WorldThread* worldThread = new WorldThread(port, host.c_str());
	worldThread->open(0);


	//************************************************************************************************************************
	// 2. Start the remote access listener thread
	//************************************************************************************************************************
	RAThread* raThread = NULL;
	if (sConfig.GetBoolDefault("Ra.Enable", false))
	{
		port = sConfig.GetIntDefault("Ra.Port", 3443);
		host = sConfig.GetStringDefault("Ra.IP", "0.0.0.0");

		raThread = new RAThread(port, host.c_str());
		raThread->open(0);
	}

	//************************************************************************************************************************
	// 3. Start the SOAP listener thread, if enabled
	//************************************************************************************************************************
#ifdef ENABLE_SOAP
	std::shared_ptr<std::thread> soapThread;
	if (sConfig.GetBoolDefault("SOAP.Enabled", false))
	{
		soapThread.reset(new std::thread(SoapThread, sConfig.GetStringDefault("SOAP.IP", "127.0.0.1"), uint16(sConfig.GetIntDefault("SOAP.Port", 7878))), [](std::thread* thread)
			{
				thread->join();
				delete thread;
			});
	}
#else /* ENABLE_SOAP */
	if (sConfig.GetBoolDefault("SOAP.Enabled", false))
	{
		sLog.outError("SOAP is enabled but wasn't included during compilation, not activating it.");
	}
#endif /* ENABLE_SOAP */


	//************************************************************************************************************************
	// 4. Start the freeze catcher thread
	//************************************************************************************************************************
	AntiFreezeThread* freezeThread = new AntiFreezeThread(1000 * sConfig.GetIntDefault("MaxCoreStuckTime", 0));
	freezeThread->open(NULL);


	//************************************************************************************************************************
	// 5. Start the console thread
	//************************************************************************************************************************
	CliThread* cliThread = NULL;
#ifdef _WIN32
	if (sConfig.GetBoolDefault("Console.Enable", true) && (m_ServiceStatus == -1)/* need disable console in service mode*/)
#else
	if (sConfig.GetBoolDefault("Console.Enable", true))
#endif
	{
		///- Launch CliRunnable thread
		cliThread = new CliThread(sConfig.GetBoolDefault("BeepAtStart", true));
		cliThread->activate();
	}
	// 阻塞直到世界线程退出
	worldThread->wait();

	if (cliThread)
	{
		cliThread->cli_shutdown();
		delete cliThread;
	}

	ACE_Thread_Manager::instance()->wait();
	sLog.outString("Halting process...");

	///- Stop freeze protection before shutdown tasks
	if (freezeThread)
	{
		delete freezeThread;
	}

	if (raThread)
	{
		delete raThread;
	}

	delete worldThread;

	///- Remove signal handling before leaving
	unhook_signals();

	///- Set server offline in realmlist
	LoginDatabase.DirectPExecute("UPDATE `realmlist` SET `realmflags` = `realmflags` | %u WHERE `id` = '%u'", REALM_FLAG_OFFLINE, realmID);

	///- Clean account database before leaving
	clear_online_accounts();

	// send all still queued mass mails (before DB connections shutdown)
	sMassMailMgr.Update(true);

	///- Wait for DB delay threads to end
	CharacterDatabase.HaltDelayThread();
	WorldDatabase.HaltDelayThread();
	LoginDatabase.HaltDelayThread();

	// This is done to make sure that we cleanup our so file before it's
	// unloaded automatically, since the ~ScriptMgr() is called to late
	// as it's allocated with static storage.
	sScriptMgr.UnloadScriptLibrary();

	///- Exit the process with specified return value
	int code = World::GetExitCode();

#ifdef WIN32
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

	sLog.outString("Bye!");
	return code;
}
/// @}
