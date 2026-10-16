import QtQuick

/// A photo from `source`, cut to a circle; `ready` once it has loaded and shows.
Canvas {
    id: root

    property url source
    property bool ready: false

    function load() {
        // Cleared now, so a photo that changed or went away does not linger.
        ready = false
        requestPaint()
        if (source.toString() === "")
            return
        // Already loaded for another canvas, it says nothing more.
        if (isImageLoaded(source)) {
            ready = true
            requestPaint()
            return
        }
        loadImage(source)
    }

    Component.onCompleted: load()
    onSourceChanged: load()
    // A paint asked for before the canvas can draw is dropped.
    onAvailableChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onImageLoaded: {
        ready = isImageLoaded(source)
        requestPaint()
    }
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        // Only a photo that loaded can be drawn; drawImage takes its address as a string.
        if (!ready)
            return
        ctx.beginPath()
        ctx.arc(width / 2, height / 2, width / 2, 0, 2 * Math.PI)
        ctx.clip()
        ctx.drawImage(source.toString(), 0, 0, width, height)
    }
}
