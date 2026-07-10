/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: ToolWidget.cpp
 *
 * Date: 2026-07-04
 *
 * Author: turnarond
 *
 * Description: Tool 前端基类实现 — 为 CMake AUTOMOC 提供编译单元，
 *              确保 Q_OBJECT 宏生成的元对象代码被链接。
 */

#include "framework/ToolWidget.h"

// 此文件的主要目的是触发 CMake AUTOMOC 为 ToolWidget 生成
// MOC 输出（metaObject/staticMetaObject/qt_metacall 等）。
// ToolWidget 是纯虚基类，本身没有需要在此实现的方法。
// 析构函数实现在头文件中（= default），无需重定义。
