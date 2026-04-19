#ifndef PAGINATIONCONTROLLER_H
#define PAGINATIONCONTROLLER_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>

class PaginationController : public QWidget
{
    Q_OBJECT

public:
    explicit PaginationController(QWidget *parent = nullptr);

    void setCurrentPage(int page);
    void setTotalPages(int totalPages);
    void setTotalRecords(int totalRecords);
    void setPageSize(int pageSize);
    void setLoading(bool loading);

    int getCurrentPage() const { return m_currentPage; }
    int getPageSize() const { return m_pageSize; }

private:
    void setupUI();

signals:
    void pageChanged(int page);
    void pageSizeChanged(int pageSize);
    void refreshRequested();

private slots:
    void onFirstPage();
    void onPreviousPage();
    void onNextPage();
    void onLastPage();
    void onPageSpinBoxChanged(int page);
    void onPageSizeChanged(int pageSize);
    void onDebounceTimeout();  // 防抖超时处理

private:
    void updateUI();
    void updatePageInfo();

    QHBoxLayout *m_layout;
    QPushButton *m_firstButton;
    QPushButton *m_previousButton;
    QPushButton *m_nextButton;
    QPushButton *m_lastButton;
    QLabel *m_pageInfoLabel;
    QSpinBox *m_pageSpinBox;
    QLabel *m_pageSizeLabel;
    QComboBox *m_pageSizeComboBox;
    QPushButton *m_refreshButton;
    QProgressBar *m_progressBar;

    int m_currentPage;
    int m_totalPages;
    int m_totalRecords;
    int m_pageSize;
    bool m_loading;

    // 性能优化
    QTimer *m_debounceTimer;  // 防抖定时器
    int m_pendingPage;  // 待处理的页面
};

#endif // PAGINATIONCONTROLLER_H
