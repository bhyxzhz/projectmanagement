#include "uistatemanager.h"
#include <QLabel>
#include <QProgressBar>
#include <QDebug>

UIStateManager::UIStateManager(QObject *parent)
    : QObject(parent)
    , m_currentSelectedRow(-1)
    , m_shouldSelectNewRecord(false)
    , m_shouldSelectEditedRecord(false)
    , m_editedRecordRow(-1)
    , m_usePagination(true)
    , m_isSearchMode(false)
{
    // 注意：m_shouldSelectNewRecord, m_shouldSelectEditedRecord, m_editedRecordRow
    // 在头文件中声明为public成员，允许MainWindow直接访问以处理数据加载后的选择逻辑
}

void UIStateManager::setCurrentSelectedRow(int row)
{
    if (m_currentSelectedRow != row) {
        m_currentSelectedRow = row;
        emit selectionChanged(row);
    }
}

void UIStateManager::clearSelection()
{
    setCurrentSelectedRow(-1);
}

void UIStateManager::markShouldSelectNewRecord()
{
    m_shouldSelectNewRecord = true;
    m_shouldSelectEditedRecord = false;
    m_editedRecordRow = -1;
}

void UIStateManager::markShouldSelectEditedRecord(int row)
{
    m_shouldSelectEditedRecord = true;
    m_shouldSelectNewRecord = false;
    m_editedRecordRow = row;
}

void UIStateManager::clearSelectionMarks()
{
    m_shouldSelectNewRecord = false;
    m_shouldSelectEditedRecord = false;
    m_editedRecordRow = -1;
}

void UIStateManager::setPaginationMode(bool enabled)
{
    if (m_usePagination != enabled) {
        m_usePagination = enabled;
        emit modeChanged(enabled);
    }
}

void UIStateManager::setSearchMode(bool enabled)
{
    m_isSearchMode = enabled;
}

void UIStateManager::updateStatusBar(QLabel *statusLabel, int totalRecords, double totalBudget, const QStringList &statusStats)
{
    if (!statusLabel) {
        return;
    }

    if (m_usePagination) {
        statusLabel->setText(QString("高性能分页模式已启用 (共%1条记录)").arg(totalRecords));
    } else {
        QString statusText = QString("总项目数: %1 | 总预算: %2万元 | %3")
                                 .arg(totalRecords)
                                 .arg(totalBudget, 0, 'f', 2)
                                 .arg(statusStats.join(" | "));
        statusLabel->setText(statusText);
    }
}

void UIStateManager::updateProgressBar(QProgressBar *progressBar, bool visible, int value, int maximum)
{
    if (!progressBar) {
        return;
    }

    progressBar->setVisible(visible);
    if (visible) {
        if (maximum > 0) {
            progressBar->setRange(0, maximum);
            progressBar->setValue(value >= 0 ? value : 0);
        } else {
            progressBar->setRange(0, 0); // 不确定进度模式
        }
    }
}
