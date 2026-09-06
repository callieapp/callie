install(FILES ${CALLIE_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES ${CALLIE_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

# The logo doubles as the application icon. A scalable SVG is enough for
# hicolor, so no rasterised sizes are shipped.
install(FILES ${CMAKE_SOURCE_DIR}/assets/logo.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${CALLIE_APP_ID}.svg
)
