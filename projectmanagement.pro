QT       += core gui sql widgets printsupport


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    databasemanager.cpp \
    main.cpp \
    mainwindow.cpp \
    paginationcontroller.cpp \
    simpledialog.cpp \
    simplepaginationmodel.cpp \
    queryqueue.cpp \
    performancemonitor.cpp \
    projectservice.cpp \
    searchservice.cpp \
    exportservice.cpp \
    reportservice.cpp \
    reportwindow.cpp \
    uistatemanager.cpp \
    errorhandler.cpp \
    configmanager.cpp \
    systemlogwindow.cpp \
    usermanagementwindow.cpp \
    usereditdialog.cpp \
    permissionmanager.cpp \
    loginwindow.cpp

HEADERS += \
    databasemanager.h \
    mainwindow.h \
    paginationcontroller.h \
    simpledialog.h \
    simplepaginationmodel.h \
    queryqueue.h \
    performancemonitor.h \
    projectservice.h \
    searchservice.h \
    exportservice.h \
    reportservice.h \
    reportwindow.h \
    uistatemanager.h \
    appconstants.h \
    errorhandler.h \
    idatabaseaccessor.h \
    configmanager.h \
    systemlogwindow.h \
    usermanagementwindow.h \
    usereditdialog.h \
    permissionmanager.h \
    loginwindow.h

# 未使用的类（保留作为技术参考，详见 README_未使用类说明.md）
# virtualscrollingmodel.h
# virtualscrollingmodel.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    images.qrc
