#ifndef ZIPFX_POWER_MANAGER_H
#define ZIPFX_POWER_MANAGER_H

#include <wx/wx.h>
#include <wx/arrstr.h>
#ifdef _WIN32
#include <powrprof.h>
#pragma comment(lib, "powrprof.lib")
#endif

enum class AfterAction
{
    Nothing,
    Sleep,
    Hibernate,
    Shutdown
};

inline wxArrayString GetAfterActionLabels()
{
    return {
        _("Do nothing"),
        _("Sleep"),
        _("Hibernate"),
        _("Shut down")
    };
}

inline bool ExecuteAfterAction(AfterAction action)
{
    if (action == AfterAction::Nothing)
        return true;

#ifdef _WIN32
    // Windows
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (action == AfterAction::Shutdown)
    {
        if (!OpenProcessToken(GetCurrentProcess(),
                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            return false;
        LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
        return ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0) != 0;
    }
    else
    {
        return SetSuspendState(
            action == AfterAction::Hibernate ? TRUE : FALSE,
            TRUE, FALSE) != 0;
    }
#elif defined(__APPLE__)
    // macOS
    if (action == AfterAction::Sleep)
        return system("pmset sleepnow") == 0;
    if (action == AfterAction::Shutdown)
        return system("osascript -e 'tell app \"System Events\" to shut down'") == 0;
    // Hibernate not directly available on macOS
    return false;
#else
    // Linux
    const char* cmd = nullptr;
    if (action == AfterAction::Sleep)
        cmd = "systemctl suspend";
    else if (action == AfterAction::Hibernate)
        cmd = "systemctl hibernate";
    else if (action == AfterAction::Shutdown)
        cmd = "systemctl poweroff";
    return cmd ? (system(cmd) == 0) : false;
#endif
}

#endif
