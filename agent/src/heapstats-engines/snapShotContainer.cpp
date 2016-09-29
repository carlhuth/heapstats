/*!
 * \file snapshotContainer.cpp
 * \brief This file is used to add up using size every class.
 * Copyright (C) 2011-2015 Nippon Telegraph and Telephone Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "globals.hpp"
#include "snapShotContainer.hpp"


/*!
 * \brief Snapshot container instance stock queue.
 */
TSnapShotQueue *TSnapShotContainer::stockQueue = NULL;

/*!
 * \brief Initialize snapshot caontainer class.
 * \return Is process succeed.
 * \warning Please call only once from main thread.
 */
bool TSnapShotContainer::globalInitialize(void) {
  try {
    /* Create snapshot container storage. */
    stockQueue = new TSnapShotQueue();
  } catch (...) {
    logger->printWarnMsg("Failure initialize snapshot container.");
    return false;
  }

  return true;
}

/*!
 * \brief Finalize snapshot caontainer class.
 * \warning Please call only once from main thread.
 */
void TSnapShotContainer::globalFinalize(void) {
  if (likely(stockQueue != NULL)) {
    /* Clear snapshot in queue. */
    TSnapShotContainer *item;
    while (stockQueue->try_pop(item)) {
      /* Deallocate snapshot instance. */
      delete item;
    }

    /* Deallocate stock queue. */
    delete stockQueue;
    stockQueue = NULL;
  }
}

/*!
 * \brief Get snapshot container instance.
 * \return Snapshot container instance.
 * \warning Don't deallocate instance getting by this function.<br>
 *          Please call "releaseInstance" method.
 */
TSnapShotContainer *TSnapShotContainer::getInstance(void) {
  TSnapShotContainer *result = NULL;
  stockQueue->try_pop(result);

  /* If need create new instance. */
  if (result == NULL) {
    /* Create new snapshot container instance. */
    try {
      result = new TSnapShotContainer();
    } catch (...) {
      result = NULL;
    }
  }

  return result;
}

/*!
 * \brief Release snapshot container instance..
 * \param instance [in] Snapshot container instance.
 * \warning Don't access instance after called this function.
 */
void TSnapShotContainer::releaseInstance(TSnapShotContainer *instance) {
  /* Sanity check. */
  if (unlikely(instance == NULL)) {
    return;
  }

  bool existStockSpace = (stockQueue->size() < MAX_STOCK_COUNT);

  if (likely(existStockSpace)) {
    /*
     * We reset this flag.
     * Because we need deallocating if failed to store to stock.
     * E.g. Failed to get mutex at "pthread_mutex_lock/unlock"
     *      or no more memory at "std::queue<T>::push()".
     */
    existStockSpace = false;

    /* Clear data. */
    instance->clear(false);

    try {
      /* Store instance. */
      stockQueue->push(instance);

      existStockSpace = true;
    } catch (...) {
      /* Maybe faield to allocate memory. So we release instance. */
    }
  }

  if (unlikely(!existStockSpace)) {
    /* Deallocate instance. */
    delete instance;
  }
}

/*!
 * \brief TSnapshotContainer constructor.
 */
TSnapShotContainer::TSnapShotContainer(void) : counterMap() {
  /* Header setting. */
  this->_header.magicNumber = conf->CollectRefTree()->get()
                                ? EXTENDED_REFTREE_SNAPSHOT
                                : EXTENDED_SNAPSHOT;
  this->_header.byteOrderMark = BOM;
  this->_header.snapShotTime = 0;
  this->_header.size = 0;
  memset((void *)&this->_header.gcCause[0], 0, 80);

  this->isCleared = true;
}

/*!
 * \brief TSnapshotContainer destructor.
 */
TSnapShotContainer::~TSnapShotContainer(void) {
  /* Cleanup elements on counter map. */
  for (auto it = counterMap.begin(); it != counterMap.end(); it++) {
    TClassCounter *clsCounter = it->second;
    if (unlikely(clsCounter == NULL)) {
      continue;
    }

    /* Deallocate field block cache. */
    free(clsCounter->offsets);

    /* Deallocate children class list. */
    TChildClassCounter *counter = clsCounter->child;
    while (counter != NULL) {
      TChildClassCounter *aCounter = counter;
      counter = counter->next;

      /* Deallocate TChildClassCounter. */
      free(aCounter->counter);
      free(aCounter);
    }

    /* Deallocate TClassCounter. */
    free(clsCounter->counter);
    free(clsCounter);
  }

  /* Clean maps. */
  counterMap.clear();
}

/*!
 * \brief Append new-class to container.
 * \param objData [in] New-class key object.
 * \return New-class data.
 */
TClassCounter *TSnapShotContainer::pushNewClass(TObjectData *objData) {
  TClassCounter *cur = NULL;

  cur = (TClassCounter *)calloc(1, sizeof(TClassCounter));
  /* If failure allocate counter data. */
  if (unlikely(cur == NULL)) {
    /* Adding empty to list is deny. */
    logger->printWarnMsg("Couldn't allocate counter memory!");
    return NULL;
  }
  cur->offsetCount = -1;

  int ret = posix_memalign(
      (void **)&cur->counter, 16,
      sizeof(TObjectCounter) /* sizeof(TObjectCounter) == 16. */);
  /* If failure allocate counter. */
  if (unlikely(ret != 0)) {
    /* Adding empty to list is deny. */
    logger->printWarnMsg("Couldn't allocate counter memory!");
    free(cur);
    return NULL;
  }

  this->clearObjectCounter(cur->counter);

  try {
    /* Set counter map. */
    counterMap[objData] = cur;
  } catch (...) {
    /*
     * Maybe failed to allocate memory at "std::map::operator[]".
     */
    free(cur->counter);
    free(cur);
    cur = NULL;
  }

  return cur;
}

/*!
 * \brief Append new-child-class to container.
 * \param clsCounter [in] Parent class counter object.
 * \param objData    [in] New-child-class key object.
 * \return New-class data.
 */
TChildClassCounter *TSnapShotContainer::pushNewChildClass(
    TClassCounter *clsCounter, TObjectData *objData) {
  TChildClassCounter *newCounter =
      (TChildClassCounter *)calloc(1, sizeof(TChildClassCounter));
  /* If failure allocate child class counter data. */
  if (unlikely(newCounter == NULL)) {
    return NULL;
  }

  int ret = posix_memalign(
      (void **)&newCounter->counter, 16,
      sizeof(TObjectCounter) /* sizeof(TObjectCounter) == 16. */);
  /* If failure allocate child class counter. */
  if (unlikely(ret != 0)) {
    free(newCounter);
    return NULL;
  }

  this->clearObjectCounter(newCounter->counter);
  newCounter->objData = objData;

  /* Chain children list. */
  spinLockWait(&clsCounter->spinlock);
  {
    TChildClassCounter *counter = clsCounter->child;
    if (unlikely(counter == NULL)) {
      clsCounter->child = newCounter;
    } else {
      /* Get last counter. */
      while (counter->next != NULL) {
        counter = counter->next;
      }
      counter->next = newCounter;
    }
  }
  spinLockRelease(&clsCounter->spinlock);

  return newCounter;
}

/*!
 * \brief Set JVM performance info to header.
 * \param info [in] JVM running performance information.
 */
void TSnapShotContainer::setJvmInfo(TJvmInfo *info) {
  /* Sanity check. */
  if (unlikely(info == NULL)) {
    logger->printWarnMsg("Couldn't get GC Information!");
    return;
  }

  /* If GC cause is need. */
  if (this->_header.cause == GC) {
    /* Copy GC cause. */
    strcpy((char *)this->_header.gcCause, info->getGCCause());
    this->_header.gcCauseLen = strlen((char *)this->_header.gcCause);

    /* Setting GC work time. */
    this->_header.gcWorktime = info->getGCWorktime();
  } else {
    /* Clear GC cause. */
    this->_header.gcCauseLen = 1;
    this->_header.gcCause[0] = '\0';

    /* GC no work. */
    this->_header.gcWorktime = 0;
  }

  /* Setting header info from TJvmInfo.
   * Total memory (JVM_TotalMemory) should be called from outside of GC.
   * Comment of VM_ENTRY_BASE macro says as following:
   *   ENTRY routines may lock, GC and throw exceptions
   * So we set TSnapShotFileHeader.totalHeapSize in TakeSnapShot() .
   */
  this->_header.FGCCount = info->getFGCCount();
  this->_header.YGCCount = info->getYGCCount();
  this->_header.newAreaSize = info->getNewAreaSize();
  this->_header.oldAreaSize = info->getOldAreaSize();
  this->_header.metaspaceUsage = info->getMetaspaceUsage();
  this->_header.metaspaceCapacity = info->getMetaspaceCapacity();
}

/*!
 * \brief Clear snapshot data.
 */
void TSnapShotContainer::clear(bool isForce) {
  if (!isForce && this->isCleared) {
    return;
  }

  /* Clean heap usage information. */
  for (auto it = counterMap.begin(); it != counterMap.end(); it++) {
    TClassCounter *clsCounter = (*it).second;
    if (unlikely(clsCounter == NULL)) {
      continue;
    }

    /* Deallocate field block cache. */
    free(clsCounter->offsets);
    clsCounter->offsets = NULL;
    clsCounter->offsetCount = -1;

    /* Reset counters. */
    this->clearChildClassCounters(clsCounter);
  }

  this->isCleared = true;
}

/*!
 * \brief Output GC statistics information.
 */
void TSnapShotContainer::printGCInfo(void) {
  logger->printInfoMsg("GC Statistics Information:");

  /* GC cause and GC worktime show only raised GC. */
  if (this->_header.cause == GC) {
    logger->printInfoMsg(
        "GC Cause: %s,  GC Worktime: " JLONG_FORMAT_STR " msec",
        (char *)this->_header.gcCause, this->_header.gcWorktime);
  }

  /* Output GC count. */
  logger->printInfoMsg("GC Count:  FullGC: " JLONG_FORMAT_STR
                       " / Young GC: " JLONG_FORMAT_STR,
                       this->_header.FGCCount, this->_header.YGCCount);

  /* Output heap size status. */
  logger->printInfoMsg("Area using size:  New: " JLONG_FORMAT_STR
                       " bytes"
                       " / Old: " JLONG_FORMAT_STR
                       " bytes"
                       " / Total: " JLONG_FORMAT_STR " bytes",
                       this->_header.newAreaSize, this->_header.oldAreaSize,
                       this->_header.totalHeapSize);

  /* Output metaspace size status. */
  const char *label =
      jvmInfo->isAfterCR6964458() ? "Metaspace usage: " : "PermGen usage: ";
  logger->printInfoMsg("%s " JLONG_FORMAT_STR
                       " bytes"
                       ", capacity: " JLONG_FORMAT_STR "  bytes",
                       label, this->_header.metaspaceUsage,
                       this->_header.metaspaceCapacity);
}

