import { app, BrowserWindow, ipcMain } from 'electron';
import path from 'node:path';
import started from 'electron-squirrel-startup';
import net from 'net';

// Handle creating/removing shortcuts on Windows when installing/uninstalling.
if (started) {
  app.quit();
}

const createWindow = () => {
  // Create the browser window.
  const mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
    },
  });

  // and load the index.html of the app.
  if (MAIN_WINDOW_VITE_DEV_SERVER_URL) {
    mainWindow.loadURL(MAIN_WINDOW_VITE_DEV_SERVER_URL);
  } else {
    mainWindow.loadFile(
      path.join(__dirname, `../renderer/${MAIN_WINDOW_VITE_NAME}/index.html`),
    );
  }

  // Open the DevTools.
  mainWindow.webContents.openDevTools();
};

// 定义管道地址，注意反斜杠转义
// Windows 管道路径固定格式: \\.\pipe\管道名
const PIPE_PATH = '\\\\.\\pipe\\demo_pipe';

ipcMain.on('send-to-cpp-service', (event, data) => {
  console.log('Frontend requested to send:', data);

  // 1. 将对象转为 JSON 字符串
  const jsonString = JSON.stringify(data);

  // 2. 创建连接客户端
  const client = net.connect(PIPE_PATH, () => {
    console.log('Connected to C++ Service!');
    // 3. 写入数据
    client.write(jsonString);
  });

  client.on('data', (data) => {
    console.log('Received reply from C++:', data.toString());
    client.end(); // 如果 C++ 有回传，读完后关闭
  });

  client.on('end', () => {
    console.log('Disconnected from service');
  });

  client.on('error', (err) => {
    console.error('Connection error. Is C++ service running?', err.message);
  });
});


// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', createWindow);

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  // On OS X it's common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and import them here.
