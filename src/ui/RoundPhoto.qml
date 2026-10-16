import QtQuick

/// A photo from `source`, cut to a circle; `ready` once it has loaded and shows.
Canvas {
    id: root

    property url source
    property bool ready: false

    function load() {
        ready = false
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

    // Left visible, as a hidden canvas misses the paint asked for when the photo
    // loads; it draws nothing until then.
    Component.onCompleted: load()
    onSourceChanged: load()
    onImageLoaded: {
        ready = isImageLoaded(source)
        requestPaint()
    }
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        // Drawing a photo that has not loaded is a type mismatch.
        if (!ready)
            return
        ctx.beginPath()
        ctx.arc(width / 2, height / 2, width / 2, 0, 2 * Math.PI)
        ctx.clip()
        ctx.drawImage(source, 0, 0, width, height)
    }
}
