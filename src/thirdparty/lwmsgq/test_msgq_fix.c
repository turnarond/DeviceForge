#include <stdio.h>
#include <string.h>
#include "lwmsgq.h"

PLW_MSGQUE_S g_msgq = NULL;

// 消息处理回调函数
int MsgQueUserCallback(void* msg, int msglen, void* userctx) {
    return 0;
}

int main() {
    // 创建消息队列
    g_msgq = MsgQueCreate(10, MsgQueUserCallback, NULL);
    if (!g_msgq) {
        fprintf(stderr, "Failed to create message queue\n");
        return 1;
    }
    
    // 推入测试消息
    char test_msg[] = "Test message for pop";
    int push_result = MsgQuePush(g_msgq, test_msg, strlen(test_msg));
    if (push_result != 0) {
        fprintf(stderr, "Failed to push message queue\n");
        return -1;
    }
    
    // 使用 MsgQuePop 弹出
    void* popped_msg = NULL;
    int popped_len = 0;
    int result = MsgQuePop(g_msgq, &popped_msg, &popped_len);
    
    printf("MsgQuePop result: %d\n", result);
    if (result == 0 && popped_msg != NULL) {
        printf("Succeeded to pop message queue\n");
        printf("pop message %.*s\n", popped_len, (char*)popped_msg);
    } else {
        printf("Failed to pop message queue\n");
    }
    
    // 使用 MsgQueTryPop 弹出
    void* popped_msg2 = NULL;
    int popped_len2 = 0;
    int result2 = MsgQueTryPop(g_msgq, &popped_msg2, &popped_len2);
    
    printf("\nMsgQueTryPop result: %d\n", result2);
    if (result2 == 0 && popped_msg2 != NULL) {
        printf("Succeeded to try pop message queue\n");
        printf("pop message %.*s\n", popped_len2, (char*)popped_msg2);
    } else {
        printf("Failed to try pop message queue (expected, queue should be empty)\n");
    }
    
    // 释放消息队列
    if (g_msgq) {
        MsgQueRelease(&g_msgq);
    }
    
    return 0;
}
