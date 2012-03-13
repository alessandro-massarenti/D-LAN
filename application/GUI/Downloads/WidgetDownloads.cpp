/**
  * D-LAN - A decentralized LAN file sharing software.
  * Copyright (C) 2010-2012 Greg Burri <greg.burri@gmail.com>
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */
  
#include <Downloads/WidgetDownloads.h>
#include <ui_WidgetDownloads.h>
using namespace GUI;

#include <limits>

#include <Common/Global.h>

#include <QMenu>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

void DownloadsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
   if (!index.isValid())
      return;

   if (index.column() == 2)
   {
      Progress progress = index.data().value<Progress>();

      QStyleOptionProgressBarV2 progressBarOption;
      progressBarOption.QStyleOption::operator=(option);

      progressBarOption.minimum = 0;
      progressBarOption.maximum = 10000;
      progressBarOption.textAlignment = Qt::AlignHCenter;
      progressBarOption.progress = progress.progress;

      switch(progress.status)
      {
      case Protos::GUI::State_Download_Status_QUEUED:
         progressBarOption.text = "Queued";
         break;
      case Protos::GUI::State_Download_Status_GETTING_THE_HASHES:
         progressBarOption.text = "Getting the hashes..";
         break;
      case Protos::GUI::State_Download_Status_DOWNLOADING:
         progressBarOption.text = QString("%1%").arg(static_cast<double>(progress.progress) / 100);
         break;
      case Protos::GUI::State_Download_Status_COMPLETE:
         progressBarOption.text = "Complete";
         break;
      case Protos::GUI::State_Download_Status_PAUSED:
         progressBarOption.text = "Paused";
         break;
      default:
         progressBarOption.text = "Waiting..";
         break;
      }

      progressBarOption.textVisible = true;

      QApplication::style()->drawControl(QStyle::CE_ProgressBar, &progressBarOption, painter);
   }
   else
   {
      // Remove the focus box, not very useful.
      QStyleOptionViewItemV4 newOption(option);
      newOption.state = option.state & (~QStyle::State_HasFocus);
      QStyledItemDelegate::paint(painter, newOption, index);
   }
}

QSize DownloadsDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
   QSize size = QStyledItemDelegate::sizeHint(option, index);

   if (index.column() == 2)
      size.setWidth(120);
   return size;
}

/////

WidgetDownloads::WidgetDownloads(QSharedPointer<RCC::ICoreConnection> coreConnection, const PeerListModel& peerListModel, const DirListModel& sharedDirsModel, QWidget *parent) :
   QWidget(parent), ui(new Ui::WidgetDownloads), coreConnection(coreConnection), downloadsModel(coreConnection, peerListModel, sharedDirsModel, checkBoxModel)
{
   this->ui->setupUi(this);

   this->ui->tblDownloads->setModel(&this->downloadsModel);
   this->ui->tblDownloads->setItemDelegate(&this->downloadsDelegate);

   this->ui->tblDownloads->setDragEnabled(true);
   this->ui->tblDownloads->setDragDropMode(QAbstractItemView::InternalMove);
   this->ui->tblDownloads->setDropIndicatorShown(true);

   this->ui->tblDownloads->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
   this->ui->tblDownloads->horizontalHeader()->setVisible(false);
   this->ui->tblDownloads->horizontalHeader()->setResizeMode(0, QHeaderView::Stretch);
   this->ui->tblDownloads->horizontalHeader()->setResizeMode(1, QHeaderView::ResizeToContents);
   this->ui->tblDownloads->horizontalHeader()->setResizeMode(2, QHeaderView::ResizeToContents);
   this->ui->tblDownloads->horizontalHeader()->setResizeMode(3, QHeaderView::ResizeToContents);

   //this->ui->tblChat->verticalHeader()->setResizeMode(QHeaderView::ResizeToContents);
   this->ui->tblDownloads->verticalHeader()->setResizeMode(QHeaderView::Fixed);
   this->ui->tblDownloads->verticalHeader()->setDefaultSectionSize(QApplication::fontMetrics().height() + 2);

   this->ui->tblDownloads->verticalHeader()->setVisible(false);
   this->ui->tblDownloads->setSelectionBehavior(QAbstractItemView::SelectRows);
   this->ui->tblDownloads->setSelectionMode(QAbstractItemView::ExtendedSelection);
   this->ui->tblDownloads->setShowGrid(false);
   this->ui->tblDownloads->setAlternatingRowColors(true);

   this->ui->tblDownloads->setContextMenuPolicy(Qt::CustomContextMenu);
   connect(this->ui->tblDownloads, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayContextMenuDownloads(const QPoint&)));
   connect(this->ui->tblDownloads, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(downloadDoubleClicked(const QModelIndex&)));

   connect(&this->downloadsModel, SIGNAL(globalProgressChanged()), this, SLOT(updateGlobalProgressBar()));

   connect(this->ui->butRemoveComplete, SIGNAL(clicked()), this, SLOT(removeCompletedFiles()));
   connect(this->ui->butRemoveSelected, SIGNAL(clicked()), this, SLOT(removeSelectedEntries()));
   connect(this->ui->butPause, SIGNAL(clicked()), this, SLOT(pauseSelectedEntries()));

   this->filterStatusList = new CheckBoxList(this);
   this->filterStatusList->setModel(&this->checkBoxModel);
   this->updateCheckBoxElements();
   this->ui->layTools->insertWidget(1, this->filterStatusList);

   connect(&this->checkBoxModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(filterChanged()));
}

WidgetDownloads::~WidgetDownloads()
{
   delete this->ui;
}

void WidgetDownloads::keyPressEvent(QKeyEvent* event)
{
   if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
      this->removeSelectedEntries();
   else
      QWidget::keyPressEvent(event);
}

void WidgetDownloads::changeEvent(QEvent* event)
{
   if (event->type() == QEvent::LanguageChange)
   {
      this->ui->retranslateUi(this);
      this->updateCheckBoxElements();
   }
   else
      QWidget::changeEvent(event);
}

void WidgetDownloads::displayContextMenuDownloads(const QPoint& point)
{
   // If there is at least one complete or downloading file we show a menu action to open the file location.
   bool showOpenLocation = false;
   QModelIndexList selectedRows = this->ui->tblDownloads->selectionModel()->selectedRows();
   for (QListIterator<QModelIndex> i(selectedRows); i.hasNext();)
   {
      int row = i.next().row();
      if (this->downloadsModel.isFileLocationKnown(row))
      {
         showOpenLocation = true;
         break;
      }
   }

   QPair<QList<quint64>, bool> IDs = this->getDownloadIDsToPause();

   QMenu menu;
   if (showOpenLocation)
      menu.addAction(QIcon(":/icons/ressources/explore_folder.png"), tr("Open location"), this, SLOT(openLocationSelectedEntries()));
   menu.addAction(QIcon(":/icons/ressources/remove_complete_files.png"), tr("Remove completed files"), this, SLOT(removeCompletedFiles()));
   menu.addAction(QIcon(":/icons/ressources/delete.png"), tr("Remove selected entries"), this, SLOT(removeSelectedEntries()));
   menu.addAction(QIcon(":/icons/ressources/pause.png"), IDs.second ? tr("Pause selected entries") : tr("Unpause selected entries"), this, SLOT(pauseSelectedEntries()));
   menu.exec(this->ui->tblDownloads->mapToGlobal(point));
}

void WidgetDownloads::downloadDoubleClicked(const QModelIndex& index)
{
   if (this->downloadsModel.isFileLocationKnown(index.row()))
      QDesktopServices::openUrl(QUrl("file:///" + this->downloadsModel.getPath(index.row())));
}

void WidgetDownloads::openLocationSelectedEntries()
{
   QModelIndexList selectedRows = this->ui->tblDownloads->selectionModel()->selectedRows();

   QSet<QString> locations;
   for (QListIterator<QModelIndex> i(selectedRows); i.hasNext();)
   {
      int row = i.next().row();
      if (this->downloadsModel.isFileLocationKnown(row))
         locations.insert("file:///" + this->downloadsModel.getPath(row, false));
   }

   for (QSetIterator<QString> i(locations); i.hasNext();)
      QDesktopServices::openUrl(QUrl(i.next(), QUrl::TolerantMode));
}

void WidgetDownloads::removeCompletedFiles()
{
   this->coreConnection->cancelDownloads(QList<quint64>(), true);
}

void WidgetDownloads::removeSelectedEntries()
{
   QList<quint64> downloadIDs;

   QModelIndexList selectedRows = this->ui->tblDownloads->selectionModel()->selectedRows();
   bool allComplete = true;
   for (QListIterator<QModelIndex> i(selectedRows); i.hasNext();)
   {
      const int row = i.next().row();
      quint64 ID = this->downloadsModel.getDownloadID(row);
      if (ID != 0)
         downloadIDs << ID;
      if (!this->downloadsModel.isFileComplete(row))
         allComplete = false;
   }

   if (!downloadIDs.isEmpty())
   {
      if (!allComplete)
      {
         QMessageBox msgBox(this);
         msgBox.setWindowIcon(QIcon(":/icons/ressources/delete.png"));
         msgBox.setWindowTitle("Remove selected downloads");
         msgBox.setText("Are you sure to remove the selected downloads? There is one or more unfinished download.");
         msgBox.setIcon(QMessageBox::Question);
         msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
         msgBox.setDefaultButton(QMessageBox::Ok);
         if (msgBox.exec() == QMessageBox::Ok)
            this->coreConnection->cancelDownloads(downloadIDs);
      }
      else
         this->coreConnection->cancelDownloads(downloadIDs);
   }
}

void WidgetDownloads::pauseSelectedEntries()
{
   QPair<QList<quint64>, bool> IDs = this->getDownloadIDsToPause();

   if (!IDs.first.isEmpty())
      this->coreConnection->pauseDownloads(IDs.first, IDs.second);
}

void WidgetDownloads::filterChanged()
{
   this->coreConnection->refresh();
}

void WidgetDownloads::updateGlobalProgressBar()
{
   const quint64 bytesInQueue = this->downloadsModel.getTotalBytesInQueue();
   const quint64 bytesDownloaded = this->downloadsModel.getTotalBytesDownloadedInQueue();
   const quint64 eta = this->downloadsModel.getEta();

   this->ui->prgGlobalProgress->setValue(bytesInQueue == 0 ? 0 : bytesDownloaded * 10000LL / bytesInQueue);
   this->ui->prgGlobalProgress->setFormat(
      QString("%1 / %2%3")
         .arg(Common::Global::formatByteSize(bytesDownloaded))
         .arg(Common::Global::formatByteSize(bytesInQueue))
         .arg(eta == 0 || eta > 604800 ? "" : " (" + Common::Global::formatTime(eta) + ")")
   );
}

void WidgetDownloads::updateCheckBoxElements()
{
   this->checkBoxModel.clear(tr("<All>"));
   this->checkBoxModel.addElement(tr("Complete"), true, STATUS_COMPLETE);
   this->checkBoxModel.addElement(tr("Downloading"), true, STATUS_DOWNLOADING);
   this->checkBoxModel.addElement(tr("Queued"), true, STATUS_QUEUED);
   this->checkBoxModel.addElement(tr("Error"), true, STATUS_ERROR);
}

QPair<QList<quint64>, bool> WidgetDownloads::getDownloadIDsToPause() const
{
   QList<quint64> downloadIDs;

   QModelIndexList selectedRows = this->ui->tblDownloads->selectionModel()->selectedRows();
   bool allPaused = true;
   for (QListIterator<QModelIndex> i(selectedRows); i.hasNext();)
   {
      const int row = i.next().row();
      quint64 ID = this->downloadsModel.getDownloadID(row);
      if (ID != 0 && !this->downloadsModel.isFileComplete(row))
      {
            downloadIDs << ID;
         if (!this->downloadsModel.isDownloadPaused(row))
            allPaused = false;
      }
   }

   return qMakePair(downloadIDs, !allPaused);
}
