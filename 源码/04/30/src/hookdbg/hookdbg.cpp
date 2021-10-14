#include "windows.h"
#include "stdio.h"

LPVOID g_pfWriteFile = NULL;
CREATE_PROCESS_DEBUG_INFO g_cpdi;
BYTE g_chINT3 = 0xCC, g_chOrgByte = 0;

BOOL OnCreateProcessDebugEvent(LPDEBUG_EVENT pde)
{
    // WriteFile() API �ּ� ���ϱ�
    g_pfWriteFile = GetProcAddress(GetModuleHandleA("kernel32.dll"), "WriteFile");

    // API Hook - WriteFile()
    //   ù ��° byte �� 0xCC (INT 3) ���� ���� 
    //   (orginal byte �� ���)
    memcpy(&g_cpdi, &pde->u.CreateProcessInfo, sizeof(CREATE_PROCESS_DEBUG_INFO));
    ReadProcessMemory(g_cpdi.hProcess, g_pfWriteFile, 
                      &g_chOrgByte, sizeof(BYTE), NULL);
    WriteProcessMemory(g_cpdi.hProcess, g_pfWriteFile, 
                       &g_chINT3, sizeof(BYTE), NULL);

    return TRUE;
}

BOOL OnExceptionDebugEvent(LPDEBUG_EVENT pde)
{
    CONTEXT ctx;
    PBYTE lpBuffer = NULL;
    DWORD dwNumOfBytesToWrite, dwAddrOfBuffer, i;
    PEXCEPTION_RECORD per = &pde->u.Exception.ExceptionRecord;

    // BreakPoint exception (INT 3) �� ���
    if( EXCEPTION_BREAKPOINT == per->ExceptionCode )
    {
        // BP �ּҰ� WriteFile() �� ���
        if( g_pfWriteFile == per->ExceptionAddress )
        {
            // #1. Unhook
            //   0xCC �� ��� �κ��� original byte �� �ǵ���
            WriteProcessMemory(g_cpdi.hProcess, g_pfWriteFile, 
                               &g_chOrgByte, sizeof(BYTE), NULL);

            // #2. Thread Context ���ϱ�
            ctx.ContextFlags = CONTEXT_CONTROL;
            GetThreadContext(g_cpdi.hThread, &ctx);

            // #3. WriteFile() �� param 2, 3 �� ���ϱ�
            //   �Լ��� �Ķ���ʹ� �ش� ���μ����� ���ÿ� ������
            //   param 2 : ESP + 0x8
            //   param 3 : ESP + 0xC
            ReadProcessMemory(g_cpdi.hProcess, (LPVOID)(ctx.Esp + 0x8), 
                              &dwAddrOfBuffer, sizeof(DWORD), NULL);
            ReadProcessMemory(g_cpdi.hProcess, (LPVOID)(ctx.Esp + 0xC), 
                              &dwNumOfBytesToWrite, sizeof(DWORD), NULL);

            // #4. �ӽ� ���� �Ҵ�
            lpBuffer = (PBYTE)malloc(dwNumOfBytesToWrite+1);
            memset(lpBuffer, 0, dwNumOfBytesToWrite+1);

            // #5. WriteFile() �� ���۸� �ӽ� ���ۿ� ����
            ReadProcessMemory(g_cpdi.hProcess, (LPVOID)dwAddrOfBuffer, 
                              lpBuffer, dwNumOfBytesToWrite, NULL);
            printf("\n### original string ###\n%s\n", lpBuffer);

            // #6. �ҹ��� -> �빮�� ��ȯ
            for( i = 0; i < dwNumOfBytesToWrite; i++ )
            {
                if( 0x61 <= lpBuffer[i] && lpBuffer[i] <= 0x7A )
                    lpBuffer[i] -= 0x20;
            }

            printf("\n### converted string ###\n%s\n", lpBuffer);

            // #7. ��ȯ�� ���۸� WriteFile() ���۷� ����
            WriteProcessMemory(g_cpdi.hProcess, (LPVOID)dwAddrOfBuffer, 
                               lpBuffer, dwNumOfBytesToWrite, NULL);
            
            // #8. �ӽ� ���� ����
            free(lpBuffer);

            // #9. Thread Context �� EIP �� WriteFile() �������� ����
            //   (����� WriteFile() + 1 ��ŭ ��������)
            ctx.Eip = (DWORD)g_pfWriteFile;
            SetThreadContext(g_cpdi.hThread, &ctx);

            // #10. Debuggee ���μ����� �����Ŵ
            ContinueDebugEvent(pde->dwProcessId, pde->dwThreadId, DBG_CONTINUE);
            Sleep(0);

            // #11. API Hook
            WriteProcessMemory(g_cpdi.hProcess, g_pfWriteFile, 
                               &g_chINT3, sizeof(BYTE), NULL);

            return TRUE;
        }
    }

    return FALSE;
}

void DebugLoop()
{
    DEBUG_EVENT de;
    DWORD dwContinueStatus;

    // Debuggee �κ��� event �� �߻��� ������ ��ٸ�
    while( WaitForDebugEvent(&de, INFINITE) )
    {
        dwContinueStatus = DBG_CONTINUE;

        // Debuggee ���μ��� ���� Ȥ�� attach �̺�Ʈ
        if( CREATE_PROCESS_DEBUG_EVENT == de.dwDebugEventCode )
        {
            OnCreateProcessDebugEvent(&de);
        }
        // ���� �̺�Ʈ
        else if( EXCEPTION_DEBUG_EVENT == de.dwDebugEventCode )
        {
            if( OnExceptionDebugEvent(&de) )
                continue;
        }
        // Debuggee ���μ��� ���� �̺�Ʈ
        else if( EXIT_PROCESS_DEBUG_EVENT == de.dwDebugEventCode )
        {
            // debuggee ���� -> debugger ����
            break;
        }

        // Debuggee �� ������ �簳��Ŵ
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, dwContinueStatus);
    }
}

int main(int argc, char* argv[])
{
    DWORD dwPID;

    if( argc != 2 )
    {
        printf("\nUSAGE : hookdbg.exe <pid>\n");
        return 1;
    }

    // Attach Process
    dwPID = atoi(argv[1]);
    if( !DebugActiveProcess(dwPID) )
    {
        printf("DebugActiveProcess(%d) failed!!!\n"
               "Error Code = %d\n", dwPID, GetLastError());
        return 1;
    }

    // ����� ����
    DebugLoop();

    return 0;
}
