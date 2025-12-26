import { windowManager } from "./dist/index.js"
import fs from "fs"

async function main() {
  // active window
  const activeWindow = windowManager.getActiveWindow()
  const activeRect = activeWindow.getBounds()

  console.log("name", activeWindow.getName())
  console.log("title", activeWindow.getTitle())
  console.log("bounds", activeRect)
  console.log(`---`)

  const pointWindow = windowManager.getWindowAtPoint(1860, 1060)
  console.log("pointWindow name", pointWindow.getName())
  console.log("pointWindow title", pointWindow.getTitle())
  console.log("pointWindow bounds", pointWindow.getBounds())
  console.log(`---`)

  const windows = windowManager.getWindows()
  for (const window of windows) {
    const rect = window.getBounds()
    const isVisible = window.isVisible()
    if (rect.height > 40 && rect.width > 40 && isVisible) {
      if (rect.x >= activeRect.x &&
          rect.y >= activeRect.y &&
          rect.x + rect.width <= activeRect.x + activeRect.width &&
          rect.y + rect.height <= activeRect.y + activeRect.height) {
        continue
      }
      console.log(`---`)
      console.log("window", window)
      console.log("isVisible", window.isVisible())
      console.log("name", window.getName())
      console.log("title", window.getTitle())
      console.log("bounds", window.getBounds())
    }
  }

  console.log(`---`)

  const image = windowManager.captureWindow(pointWindow.id)
  console.log(`image: `, image.length)

  // 移除 base64 前缀（如果有）
  const base64Data = image.replace(/^data:image\/png;base64,/, '');

  // 将 base64 字符串转换为 Buffer
  const buffer = Buffer.from(base64Data, 'base64');

  // 写入文件
  fs.writeFileSync('./test.png', buffer);

  const desktopID = windowManager.getDesktopWindowID()
  console.log(`desktopID: `, desktopID)
  const image1 = windowManager.captureWindow(desktopID)
  console.log(`image1: `, image1.length)

  // 移除 base64 前缀（如果有）
  const base64Data1 = image1.replace(/^data:image\/png;base64,/, '');

  // 将 base64 字符串转换为 Buffer
  const buffer1 = Buffer.from(base64Data1, 'base64');

  // 写入文件
  fs.writeFileSync('./test1.png', buffer1);

  windowManager.cleanup()
}

main()

// console.log("---")
