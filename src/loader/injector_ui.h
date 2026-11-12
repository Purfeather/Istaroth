#pragma once
#include <windows.h>
#include <string>

namespace istaroth {

enum class InjectorStage {
    Checking,    // 正在校验游戏版本与载荷文件
    WaitingGame, // 首次使用：未配置路径，等待用户手动启动原神
    Terminating, // 正在结束旧游戏实例
    Starting,    // 正在启动游戏并挂载驻留组件
    Waiting,     // 等待游戏内就绪响应
    Complete,    // 初始化完成，即将退出
    Failed,      // 加载未完成 / 失败
    Cancelled    // 用户取消或中止
};

// 客户端设计基准尺寸：640 × 220 逻辑像素 (适配 96/144/192 DPI 等比缩放)
constexpr int kInjectorDesignWidth  = 640;
constexpr int kInjectorDesignHeight = 220;

struct InjectorUiState {
    InjectorStage stage = InjectorStage::Checking;
    std::wstring main_status;     // 核心状态文案（支持自动折行）
    std::wstring detail_hint;     // 辅助说明文案（区分加载器关闭与游戏内F10停用）
    std::wstring game_path;       // 可选游戏路径说明（主会话可填可不填）
    DWORD error_code = 0;         // 失败时的错误码（0为正常）
    float dpi_scale = 0.0f;       // 可选DPI比例（0则自动根据客户区计算）
};

// 绘制加载器工作区客户区内容 (纯 GDI / 双缓冲，支持 96/144/192 DPI 动态缩放)
void RenderInjectorUi(HDC hdc, const RECT& client_rect, const InjectorUiState& state);

} // namespace istaroth
