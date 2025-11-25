/**
 * This file will automatically be loaded by vite and run in the "renderer" context.
 * To learn more about the differences between the "main" and the "renderer" context in
 * Electron, visit:
 *
 * https://electronjs.org/docs/tutorial/process-model
 *
 * By default, Node.js integration in this file is disabled. When enabling Node.js integration
 * in a renderer process, please be aware of potential security implications. You can read
 * more about security risks here:
 *
 * https://electronjs.org/docs/tutorial/security
 *
 * To enable Node.js integration in this file, open up `main.ts` and enable the `nodeIntegration`
 * flag:
 *
 * ```
 *  // Create the browser window.
 *  mainWindow = new BrowserWindow({
 *    width: 800,
 *    height: 600,
 *    webPreferences: {
 *      nodeIntegration: true
 *    }
 *  });
 * ```
 */

// 这一段是为了让 TS 知道 window 上有个 electronAPI
export interface IElectronAPI {
  sendToService: (data: any) => void;
}

declare global {
  interface Window {
    electronAPI: IElectronAPI;
  }
}


const sendBtn = document.getElementById('sendBtn');

sendBtn?.addEventListener('click', () => {
  console.log('Button clicked!');
  
  // 构造要发送的数据
  const payload = {
    action: "update_user",
    userId: 1001,
    timestamp: Date.now(),
    message: "Hello from Electron UI!"
  };

  // 调用 Preload 暴露的接口
  window.electronAPI.sendToService(payload);
});