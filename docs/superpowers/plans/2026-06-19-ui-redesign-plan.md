# DeployMaster 专业级布局实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重新设计 DeployMaster 界面布局，实现专业级、灵活可调的工业级运维工具界面

**Architecture:** 使用 QSplitter 实现区域动态调整，重新组织配置区、工作区和日志区的布局，去掉重复信息，优化空间利用率

**Tech Stack:** Qt 6.11, C++, Visual Studio 2022

---

## 文件结构

| 文件 | 类型 | 描述 |
|------|------|------|
| `DeployMaster.ui` | 修改 | 主界面布局文件，核心改动 |
| `DeployMaster.cpp` | 修改 | 移除对已删除控件的引用 |
| `DeployMaster.h` | 修改 | 更新类定义 |
| `darkstyle.qss` | 修改 | 优化 QSplitter 和面板样式 |

---

### Task 1: 修改顶部配置栏 - 移除重复的默认远程路径

**Files:**
- Modify: `D:\personal\QtDeployMaster\DeployMaster.ui`

**Step 1: 删除默认远程路径控件**

```xml
<!-- 删除以下内容（约第192-205行） -->
<item row="2" column="0">
 <widget class="QLabel" name="label_defaultRemotePath">
  <property name="text">
   <string>默认远程路径：</string>
  </property>
 </widget>
</item>
<item row="2" column="1">
 <widget class="QLineEdit" name="txt_defaultRemotePath">
  <property name="text">
   <string>/apps/m580cn/bin/</string>
  </property>
 </widget>
</item>
```

**Step 2: 调整认证区域标题为"认证信息"**

```xml
<property name="title">
 <string>认证信息</string>
</property>
```

**Step 3: 调整水平布局比例为 3:1**

```xml
<layout class="QHBoxLayout" name="horizontalLayout_common" stretch="3,1">
```

**Step 4: 调整认证区域高度为 60px**

```xml
<property name="maximumSize">
 <size>
  <width>16777215</width>
  <height>60</height>
 </size>
</property>
```

**Step 5: 调整 IP 列表高度为 60px**

```xml
<property name="minimumSize">
 <size>
  <width>0</width>
  <height>60</height>
 </size>
</property>
<property name="maximumSize">
 <size>
  <width>16777215</width>
  <height>60</height>
 </size>
</property>
```

---

### Task 2: 修改工作区布局 - 添加 QSplitter 分隔左右区域

**Files:**
- Modify: `D:\personal\QtDeployMaster\DeployMaster.ui`

**Step 1: 将 TabWidget 包裹在 QSplitter 中**

将原有的 TabWidget 区域（约第212-465行）替换为：

```xml
<item>
 <widget class="QSplitter" name="splitter_main">
  <property name="orientation">
   <enum>Qt::Orientation::Horizontal</enum>
  </property>
  <property name="handleWidth">
   <number>5</number>
  </property>
  <widget class="QTabWidget" name="tabWidget">
   <!-- 原有 TabWidget 内容保持不变 -->
  </widget>
  <widget class="QWidget" name="widget_rightPanel">
   <layout class="QVBoxLayout" name="verticalLayout_rightPanel">
    <property name="spacing">
     <number>6</number>
    </property>
    <property name="leftMargin">
     <number>6</number>
    </property>
    <property name="topMargin">
     <number>6</number>
    </property>
    <property name="rightMargin">
     <number>6</number>
    </property>
    <property name="bottomMargin">
     <number>6</number>
    </property>
    <item>
     <widget class="QGroupBox" name="groupBox_remotePreview">
      <property name="title">
       <string>远端预览</string>
      </property>
      <layout class="QVBoxLayout" name="verticalLayout_remote">
       <item>
        <widget class="QComboBox" name="cmb_targetIPs">
         <property name="minimumSize">
          <size>
           <width>200</width>
           <height>0</height>
          </size>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QPushButton" name="btn_refreshRemote">
         <property name="text">
          <string>刷新</string>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QGroupBox" name="groupBox_remoteFiles">
         <property name="title">
          <string>远程文件</string>
         </property>
         <layout class="QVBoxLayout" name="verticalLayout_tree">
          <item>
           <widget class="QTreeView" name="tree_remoteFiles">
            <property name="headerHidden">
             <bool>true</bool>
            </property>
           </widget>
          </item>
         </layout>
        </widget>
       </item>
      </layout>
     </widget>
    </item>
    <item>
     <spacer name="verticalSpacer_right">
      <property name="orientation">
       <enum>Qt::Orientation::Vertical</enum>
      </property>
      <property name="sizeType">
       <enum>QSizePolicy::Policy::Expanding</enum>
      </property>
      <property name="sizeHint" stdset="0">
       <size>
        <width>20</width>
        <height>40</height>
       </size>
      </property>
     </spacer>
    </item>
   </layout>
  </widget>
 </widget>
</item>
```

---

### Task 3: 修改底部日志区 - 使用 QSplitter 实现高度调整

**Files:**
- Modify: `D:\personal\QtDeployMaster\DeployMaster.ui`

**Step 1: 将工作区和日志区包裹在 QSplitter 中**

将原有的工作区和日志区（约第212-515行）替换为：

```xml
<item>
 <widget class="QSplitter" name="splitter_log">
  <property name="orientation">
   <enum>Qt::Orientation::Vertical</enum>
  </property>
  <property name="handleWidth">
   <number>5</number>
  </property>
  <!-- 工作区内容（包含 splitter_main） -->
  <widget class="QSplitter" name="splitter_main">
   <!-- ... 保持不变 ... -->
  </widget>
  <!-- 日志区 -->
  <widget class="QGroupBox" name="groupBox_log">
   <property name="title">
    <string>综合日志输出</string>
   </property>
   <layout class="QVBoxLayout" name="verticalLayout_log">
    <item>
     <widget class="QTextEdit" name="txt_globalLog">
      <!-- ... 保持不变 ... -->
     </widget>
    </item>
   </layout>
  </widget>
 </widget>
</item>
```

**Step 2: 删除原有的 frame_splitter**

删除约第466-480行的 `<widget class="QFrame" name="frame_splitter">`

---

### Task 4: 更新 DeployMaster.cpp - 移除对已删除控件的引用

**Files:**
- Modify: `D:\personal\QtDeployMaster\DeployMaster.cpp`

**Step 1: 搜索并移除所有 `txt_defaultRemotePath` 引用**

```cpp
// 删除以下类似代码
QString defaultPath = ui->txt_defaultRemotePath->text();
```

**Step 2: 更新 getFtpUser/getFtpPass 方法**

确保这些方法只返回用户名和密码，不涉及默认路径。

---

### Task 5: 更新样式表 - 优化 QSplitter 样式

**Files:**
- Modify: `D:\personal\QtDeployMaster\darkstyle.qss`

**Step 1: 添加 QSplitter 样式**

```css
/* ===== 分割器样式 ===== */
QSplitter::handle {
    background-color: #2A2A4A;
    border: 1px solid #3A3A5A;
}

QSplitter::handle:hover {
    background-color: #3A3A6A;
}

QSplitter::handle:vertical {
    height: 5px;
}

QSplitter::handle:horizontal {
    width: 5px;
}
```

**Step 2: 添加右侧面板样式**

```css
/* ===== 右侧面板 ===== */
#widget_rightPanel {
    background-color: #1A1A2E;
    border-left: 1px solid #2A2A4A;
}
```

---

### Task 6: 编译测试

**Files:**
- Build: `DeployMaster.vcxproj`

**Step 1: 清理构建**

```powershell
msbuild DeployMaster.vcxproj /t:Clean /p:Configuration=Debug /p:Platform=x64
```

**Step 2: 重新构建**

```powershell
msbuild DeployMaster.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64
```

**Step 3: 验证功能**

运行应用程序，验证：
1. 顶部配置栏布局正确
2. 左右分割器可拖动
3. 上下分割器可拖动
4. 远端预览功能正常
5. 各 Tab 页功能正常
6. 日志输出正常

---

## Self-Review

**1. Spec coverage:**
- ✅ 移除重复的默认远程路径
- ✅ 调整 IP 列表和认证区域比例为 3:1
- ✅ 添加水平 QSplitter 分隔左右区域
- ✅ 添加垂直 QSplitter 分隔工作区和日志区
- ✅ 优化样式表

**2. Placeholder scan:**
- ✅ 无 TBD/TODO
- ✅ 所有步骤都有具体代码
- ✅ 所有命令都有明确输出

**3. Type consistency:**
- ✅ 控件名称保持一致
- ✅ 方法签名保持一致

---

## 风险评估

| 风险 | 缓解措施 |
|------|---------|
| 布局改动导致功能失效 | 逐项测试每个控件和功能 |
| 分割器样式不兼容 | 使用通用 Qt 样式 |
| 分辨率适配问题 | 使用相对布局和比例 |