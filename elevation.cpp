// windows.h 必须带 NOMINMAX / WIN32_LEAN_AND_MEAN 引入，否则 min/max 宏会污染后续所有头文件。
// TOKEN_ELEVATION 要求 Vista+，所以这里显式指定 _WIN32_WINNT。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

#include "elevation.h"

#include <QCoreApplication>
#include <QDir>

#include <string>

#ifdef _WIN32
namespace {

QString lastErrorText(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                           | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    QString text;
    if (length > 0 && buffer)
        text = QString::fromWCharArray(buffer, int(length)).trimmed();
    if (buffer)
        LocalFree(buffer);

    if (text.isEmpty())
        text = QStringLiteral("错误码 %1").arg(code);
    return text;
}

} // namespace
#endif // _WIN32

bool isProcessElevated()
{
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation = {};
    DWORD returned = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                       sizeof(elevation), &returned);
    CloseHandle(token);
    return ok != FALSE && elevation.TokenIsElevated != 0;
#else
    return false;
#endif
}

bool runAsAdmin(const QString &arguments, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

#ifdef _WIN32
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const std::wstring file = exePath.toStdWString();
    const std::wstring parameters = arguments.toStdWString();

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";   // 触发 UAC「以管理员身份运行」
    info.lpFile = file.c_str();
    info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&info)) {
        if (info.hProcess)
            CloseHandle(info.hProcess);
        return true;
    }

    const DWORD error = GetLastError();
    // 用户点了 UAC 的「否」：不算错误，不弹提示
    if (errorOut && error != ERROR_CANCELLED)
        *errorOut = lastErrorText(error);
    return false;
#else
    Q_UNUSED(arguments)
    if (errorOut)
        *errorOut = QStringLiteral("仅支持 Windows。");
    return false;
#endif
}
