/*
 * Copyright (c) 2024-2026 turnarond.
 * All rights reserved.
 *
 * File: FileEditorDialog.cpp
 * Date: 2026-10-14
 * Author: turnarond
 *
 * Description: F4 预览/编辑对话框实现。
 */

#include "ui/FileEditorDialog.h"
#include "ui/FileEncoding.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

FileEditorDialog::FileEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("fileEditorDialog"));
    setWindowTitle(QStringLiteral("预览/编辑"));
    resize(900, 620);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(6);

    auto* header = new QHBoxLayout();
    header->setSpacing(10);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("feTitle"));
    m_path = new QLabel(this);
    m_path->setObjectName(QStringLiteral("fePath"));
    m_badge = new QLabel(this);
    m_badge->setObjectName(QStringLiteral("feBadge"));
    m_badge->setVisible(false);
    m_codec = new QComboBox(this);
    m_codec->setObjectName(QStringLiteral("feCodec"));
    m_codec->setMinimumWidth(120);
    header->addWidget(m_title);
    header->addWidget(m_path, 1);
    header->addWidget(m_badge);
    header->addWidget(m_codec);
    v->addLayout(header);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName(QStringLiteral("fileEditorText"));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setTabStopDistance(4 * m_text->fontMetrics().horizontalAdvance(' '));
    v->addWidget(m_text, 1);

    auto* footer = new QHBoxLayout();
    footer->setSpacing(8);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("feStatus"));
    auto* saveAsBtn = new QPushButton(QStringLiteral("另存本地…"), this);
    saveAsBtn->setObjectName(QStringLiteral("feSaveAs"));
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("feClose"));
    m_saveBtn = new QPushButton(QStringLiteral("保存并上传"), this);
    m_saveBtn->setObjectName(QStringLiteral("feSave"));
    footer->addWidget(m_status, 1);
    footer->addWidget(saveAsBtn);
    footer->addWidget(closeBtn);
    footer->addWidget(m_saveBtn);
    v->addLayout(footer);

    connect(m_text, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_binding)
            m_dirty = true;
    });
    connect(m_codec, &QComboBox::currentTextChanged, this, [this](const QString& c) {
        if (m_binding || c.isEmpty())
            return;
        emit codecChangedRequest(c);
    });
    connect(m_saveBtn, &QPushButton::clicked, this, &FileEditorDialog::saveRequested);
    connect(saveAsBtn, &QPushButton::clicked, this, &FileEditorDialog::doSaveAsLocal);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

void FileEditorDialog::bind(const QString& fileName, const QString& fullPath, const QString& text,
                           const QStringList& codecs, const QString& codec, const QString& eol,
                           bool fromBom, const QString& statusNote,
                           bool readOnly, const QString& readOnlyReason)
{
    m_binding = true;
    setWindowTitle(fileName + QStringLiteral(" — 预览/编辑"));
    m_title->setText(fileName);
    m_path->setText(fullPath);
    m_path->setToolTip(fullPath);

    m_codec->clear();
    m_codec->addItems(codecs);
    m_codec->setCurrentText(codec);

    m_text->setPlainText(text);
    m_eol = eol;
    m_fromBom = fromBom;
    m_dirty = false;

    m_badge->setVisible(readOnly);
    if (readOnly)
        m_badge->setText(readOnlyReason);
    m_text->setReadOnly(readOnly);
    m_saveBtn->setEnabled(!readOnly);
    m_status->setText(statusNote);
    m_binding = false;
}

QString FileEditorDialog::currentCodec() const
{
    return m_codec->currentText();
}

void FileEditorDialog::setCodecSilently(const QString& codec)
{
    const QSignalBlocker b(m_codec);
    m_codec->setCurrentText(codec);
}

QByteArray FileEditorDialog::currentBytes(bool *fellBackToUtf8) const
{
    return encodeText(m_text->toPlainText(), m_codec->currentText(), m_eol, m_fromBom,
                      fellBackToUtf8);
}

void FileEditorDialog::markSaved()
{
    m_dirty = false;
}

void FileEditorDialog::setBusy(bool busy)
{
    m_saveBtn->setDisabled(busy);
    m_saveBtn->setText(busy ? QStringLiteral("上传中…") : QStringLiteral("保存并上传"));
}

void FileEditorDialog::setStatusNote(const QString& note)
{
    m_status->setText(note);
}

void FileEditorDialog::setDiscardGuard(std::function<bool()> guard)
{
    m_discardGuard = std::move(guard);
}

void FileEditorDialog::closeEvent(QCloseEvent* event)
{
    if (m_dirty && m_discardGuard && !m_discardGuard()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void FileEditorDialog::doSaveAsLocal()
{
    const QString target = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存本地"), QFileInfo(m_path->text()).fileName());
    if (target.isEmpty())
        return;
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusNote(QStringLiteral("另存失败：%1").arg(f.errorString()));
        return;
    }
    f.write(currentBytes());
    f.close();
    setStatusNote(QStringLiteral("已另存本地 %1").arg(QFileInfo(target).fileName()));
}
