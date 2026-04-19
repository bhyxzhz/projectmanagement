#include "paginationcontroller.h"
#include "appconstants.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QTimer>
#include <QDebug>

PaginationController::PaginationController(QWidget *parent)
    : QWidget(parent)
    , m_currentPage(1)
    , m_totalPages(1)
    , m_totalRecords(0)
    , m_pageSize(PaginationConstants::DEFAULT_PAGE_SIZE)
    , m_loading(false)
    , m_debounceTimer(new QTimer(this))
    , m_pendingPage(-1)
{
    setupUI();
    updateUI();

    // 配置防抖定时器（避免频繁触发页面切换）
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(PaginationConstants::LOAD_TIMER_INTERVAL_MS);
    connect(m_debounceTimer, &QTimer::timeout, this, &PaginationController::onDebounceTimeout);
}

void PaginationController::setupUI()
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(5, 5, 5, 5);

    // 第一页按钮
    m_firstButton = new QPushButton("首页");
    m_firstButton->setMaximumWidth(60);
    connect(m_firstButton, &QPushButton::clicked, this, &PaginationController::onFirstPage);

    // 上一页按钮
    m_previousButton = new QPushButton("上一页");
    m_previousButton->setMaximumWidth(80);
    connect(m_previousButton, &QPushButton::clicked, this, &PaginationController::onPreviousPage);

    // 页码信息
    m_pageInfoLabel = new QLabel();
    m_pageInfoLabel->setMinimumWidth(200);

    // 页码输入框
    m_pageSpinBox = new QSpinBox();
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setMaximumWidth(80);
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PaginationController::onPageSpinBoxChanged);

    // 下一页按钮
    m_nextButton = new QPushButton("下一页");
    m_nextButton->setMaximumWidth(80);
    connect(m_nextButton, &QPushButton::clicked, this, &PaginationController::onNextPage);

    // 最后一页按钮
    m_lastButton = new QPushButton("末页");
    m_lastButton->setMaximumWidth(60);
    connect(m_lastButton, &QPushButton::clicked, this, &PaginationController::onLastPage);

    // 页面大小标签
    m_pageSizeLabel = new QLabel("每页显示:");
    m_pageSizeLabel->setMaximumWidth(80);

    // 页面大小选择框
    m_pageSizeComboBox = new QComboBox();
    m_pageSizeComboBox->addItems({"3000", "5000", "10000", "20000"});
    m_pageSizeComboBox->setCurrentText("3000");
    m_pageSizeComboBox->setMaximumWidth(80);
    connect(m_pageSizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaginationController::onPageSizeChanged);

    // 刷新按钮
    m_refreshButton = new QPushButton("刷新");
    m_refreshButton->setMaximumWidth(60);
    connect(m_refreshButton, &QPushButton::clicked, this, &PaginationController::refreshRequested);

    // 进度条
    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumWidth(150);
    m_progressBar->setVisible(false);

    // 添加到布局
    m_layout->addWidget(m_firstButton);
    m_layout->addWidget(m_previousButton);
    m_layout->addWidget(m_pageInfoLabel);
    m_layout->addWidget(m_pageSpinBox);
    m_layout->addWidget(m_nextButton);
    m_layout->addWidget(m_lastButton);
    m_layout->addStretch();
    m_layout->addWidget(m_pageSizeLabel);
    m_layout->addWidget(m_pageSizeComboBox);
    m_layout->addWidget(m_refreshButton);
    m_layout->addWidget(m_progressBar);
}

void PaginationController::setCurrentPage(int page)
{
    if (page != m_currentPage && page >= 1 && page <= m_totalPages) {
        m_currentPage = page;
        updateUI();
    }
}

void PaginationController::setTotalPages(int totalPages)
{
    if (totalPages != m_totalPages) {
        qDebug() << "分页控制器: 设置总页数从" << m_totalPages << "到" << totalPages;
        m_totalPages = totalPages;
        m_pageSpinBox->setMaximum(totalPages);
        updateUI();
    }
}

void PaginationController::setTotalRecords(int totalRecords)
{
    if (totalRecords != m_totalRecords) {
        m_totalRecords = totalRecords;
        updatePageInfo();
    }
}

void PaginationController::setPageSize(int pageSize)
{
    if (pageSize != m_pageSize) {
        m_pageSize = pageSize;
        // 确保下拉框显示正确的值
        QString pageSizeStr = QString::number(pageSize);
        m_pageSizeComboBox->setCurrentText(pageSizeStr);
        updateUI();

        // 关键：强制发射一次刷新信号，确保模型重新计算总页数
        emit refreshRequested();
    }
}

void PaginationController::setLoading(bool loading)
{
    if (loading != m_loading) {
        m_loading = loading;
        m_progressBar->setVisible(loading);
        if (loading) {
            m_progressBar->setRange(0, 0); // 不确定进度
        }
        updateUI();
    }
}

void PaginationController::updateUI()
{
    qDebug() << "分页控制器: 更新UI - 当前页:" << m_currentPage << "总页数:" << m_totalPages << "加载中:" << m_loading;

    // 更新按钮状态
    m_firstButton->setEnabled(!m_loading && m_currentPage > 1);
    m_previousButton->setEnabled(!m_loading && m_currentPage > 1);
    m_nextButton->setEnabled(!m_loading && m_currentPage < m_totalPages);
    m_lastButton->setEnabled(!m_loading && m_currentPage < m_totalPages);
    m_pageSpinBox->setEnabled(!m_loading);
    m_pageSizeComboBox->setEnabled(!m_loading);
    m_refreshButton->setEnabled(!m_loading);

    qDebug() << "分页控制器: 按钮状态 - 首页:" << m_firstButton->isEnabled()
             << "上一页:" << m_previousButton->isEnabled()
             << "下一页:" << m_nextButton->isEnabled()
             << "末页:" << m_lastButton->isEnabled();

    // 更新页码输入框
    m_pageSpinBox->blockSignals(true);
    m_pageSpinBox->setValue(m_currentPage);
    m_pageSpinBox->blockSignals(false);

    updatePageInfo();
}

void PaginationController::updatePageInfo()
{
    QString info = QString("第 %1 页，共 %2 页，总计 %3 条记录")
                       .arg(m_currentPage)
                       .arg(m_totalPages)
                       .arg(m_totalRecords);
    m_pageInfoLabel->setText(info);
}

void PaginationController::onFirstPage()
{
    if (m_currentPage > 1 && !m_loading) {
        m_pendingPage = 1;
        m_debounceTimer->start();
    }
}

void PaginationController::onPreviousPage()
{
    if (m_currentPage > 1 && !m_loading) {
        m_pendingPage = m_currentPage - 1;
        m_debounceTimer->start();
    }
}

void PaginationController::onNextPage()
{
    if (m_currentPage < m_totalPages && !m_loading) {
        m_pendingPage = m_currentPage + 1;
        m_debounceTimer->start();
    }
}

void PaginationController::onLastPage()
{
    if (m_currentPage < m_totalPages && !m_loading) {
        m_pendingPage = m_totalPages;
        m_debounceTimer->start();
    }
}

void PaginationController::onPageSpinBoxChanged(int page)
{
    if (page != m_currentPage && page >= 1 && page <= m_totalPages) {
        emit pageChanged(page);
    }
}

void PaginationController::onPageSizeChanged(int index)
{
    Q_UNUSED(index);  // 告诉编译器这个参数是有意未使用的

    // 直接从下拉框获取当前文本，不使用参数（参数可能不可靠）
    QString currentText = m_pageSizeComboBox->currentText();
    if (currentText.isEmpty()) {
        return;
    }

    int newPageSize = currentText.toInt();

    // 强制发射信号，不管是否与当前值相同
    // 这样能确保从任何值切换到任何值都会触发更新
    qDebug() << "onPageSizeChanged: currentText=" << currentText << "newPageSize=" << newPageSize;
    emit pageSizeChanged(newPageSize);
}

void PaginationController::onDebounceTimeout()
{
    if (m_pendingPage > 0 && m_pendingPage != m_currentPage) {
        emit pageChanged(m_pendingPage);
        m_pendingPage = -1;
    }
}
