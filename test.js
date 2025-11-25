import { windowManager } from "./dist/index.js"

async function main() {
  // active window
  const activeWindow = windowManager.getActiveWindow()
  const activeRect = activeWindow.getBounds()

  console.log("name", activeWindow.getName())
  console.log("title", activeWindow.getTitle())
  console.log("bounds", activeRect)
  console.log(`---`)

  const pointWindow = windowManager.getWindowAtPoint(200, 200)
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
}

main()
