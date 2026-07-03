#ifndef MAC_TITLE_BAR_H
#define MAC_TITLE_BAR_H

#ifdef __cplusplus
extern "C" {
#endif

void SetupMacTitleBar(void* windowHandle);
void SetMacTitleBarVisible(void* windowHandle, bool visible);

#ifdef __cplusplus
}
#endif

#endif // MAC_TITLE_BAR_H
