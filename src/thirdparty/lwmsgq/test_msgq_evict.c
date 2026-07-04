#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwmsgq.h"

void test_msgq_evict() {
    printf("=== Test MsgQuePushWithEvict and Release ===\n");
    
    // 创建大小为10的消息队列
    PLW_MSGQUE_S msgq = MsgQueCreate(10, NULL, NULL);
    if (!msgq) {
        printf("MsgQueCreate failed!\n");
        return;
    }
    printf("MsgQueCreate success\n");
    
    // 填充10个消息，使队列满
    char* msgs[11];
    for (int i = 0; i < 10; i++) {
        msgs[i] = (char*)malloc(20);
        sprintf(msgs[i], "Message %d", i);
        int ret = MsgQuePush(msgq, msgs[i], strlen(msgs[i]) + 1);
        if (ret != 0) {
            printf("MsgQuePush failed at %d, ret=%d\n", i, ret);
            MsgQueRelease(&msgq);
            return;
        }
    }
    printf("Fill 10 messages, queue is full\n");
    printf("Current used nums: %d\n", MsgQueGetUsedNums(msgq));
    
    // 调用MsgQuePushWithEvict，推送第11个消息
    msgs[10] = (char*)malloc(20);
    sprintf(msgs[10], "Message 10");
    void* popped_msg = NULL;
    int ret = MsgQuePushWithEvict(msgq, msgs[10], strlen(msgs[10]) + 1, &popped_msg);
    if (ret != 0) {
        printf("MsgQuePushWithEvict failed, ret=%d\n", ret);
        MsgQueRelease(&msgq);
        return;
    }
    printf("MsgQuePushWithEvict success, popped msg: %s\n", (char*)popped_msg);
    printf("Current used nums: %d\n", MsgQueGetUsedNums(msgq));
    
    // 记录被弹出的消息索引
    int popped_idx = -1;
    for (int i = 0; i < 10; i++) {
        if (msgs[i] == popped_msg) {
            popped_idx = i;
            break;
        }
    }
    
    // 释放队列
    printf("Releasing queue...\n");
    MsgQueRelease(&msgq);
    printf("Queue released successfully! No crash.\n");
    
    // 释放消息，跳过被弹出的消息
    for (int i = 0; i < 11; i++) {
        if (i != popped_idx) {
            free(msgs[i]);
        }
    }
    
    // 释放被弹出的消息
    free(popped_msg);
    
    printf("=== Test completed successfully ===\n");
}

int main() {
    test_msgq_evict();
    return 0;
}
