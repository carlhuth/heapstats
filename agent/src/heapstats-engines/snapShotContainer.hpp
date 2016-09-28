/*!
 * \file snapshotContainer.hpp
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

#ifndef _SNAPSHOT_CONTAINER_HPP
#define _SNAPSHOT_CONTAINER_HPP

#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>

#include "jvmInfo.hpp"
#include "oopUtil.hpp"

#if PROCESSOR_ARCH == X86
#include "arch/x86/lock.inline.hpp"
#elif PROCESSOR_ARCH == ARM
#include "arch/arm/lock.inline.hpp"
#endif

/* Magic number macro. */

/*!
 * \brief Magic number of snapshot file format.<br />
 *        49 ... HeapStats 1.0 format.<br />
 *        61 ... HeapStats 1.1 format.<br />
 *   Extended magic number is represented as logical or.
 *   Meanings of each field are as below:
 *     0b10000000: This SnapShot is 2.0 format.
 *                 It contains snapshot and metaspace data.
 *     0b00000001: This SnapShot contains reference data.
 *       Other fields (bit 1 - 6) are reserved.
 * \warning Don't change output snapshot format, if you change this value.
 */
#define EXTENDED_SNAPSHOT         0b10000000
#define EXTENDED_REFTREE_SNAPSHOT 0b10000001

/*!
 * \brief This structure stored class size and number of class-instance.
 */
#pragma pack(push, 1)
typedef struct {
  jlong count;      /*!< Class instance count. */
  jlong total_size; /*!< Class total use size. */
} TObjectCounter;

/*!
 * \brief This structure stored class information.
 */
typedef struct {
  jlong tag;          /*!< Class tag.                                 */
  jlong classNameLen; /*!< Class name.                                */
  char *className;    /*!< Class name length.                         */
  void *klassOop;     /*!< Java inner class object.                   */
  jlong oldTotalSize; /*!< Class old total use size.                  */
  TOopType oopType;   /*!< Type of class.                             */
  jlong clsLoaderId;  /*!< Class loader instance id.                  */
  jlong clsLoaderTag; /*!< Class loader class tag.                    */
  bool isRemoved;     /*!< Class is already unloaded.                 */
  jlong instanceSize; /*!< Class size if this class is instanceKlass. */
} TObjectData;

/*!
 * \brief This structure stored child class size information.
 */
struct TChildClassCounter {
  TObjectCounter *counter;  /*!< Java inner class object. */
  TObjectData *objData;     /*!< Class information.       */
  TChildClassCounter *next; /*!< Pointer of next object.  */
  unsigned int callCount;   /*!< Call count.              */
};

/*!
 * \brief This structure stored class and children class size information.
 */
typedef struct {
  TObjectCounter *counter;   /*!< Java inner class object.  */
  TChildClassCounter *child; /*!< Child class informations. */
  volatile int spinlock;     /*!< Spin lock object.         */
  TOopMapBlock *offsets;     /*!< Offset list.              */
  int offsetCount;           /*!< Count of offset list.     */
} TClassCounter;

/*!
 * \brief This structure stored snapshot information.
 */
typedef struct {
  char magicNumber;        /*!< Magic number for format.              */
  char byteOrderMark;      /*!< Express byte order.                   */
  jlong snapShotTime;      /*!< Datetime of take snapshot.            */
  jlong size;              /*!< Class entries count.                  */
  jint cause;              /*!< Cause of snapshot.                    */
  jlong gcCauseLen;        /*!< Length of GC cause.                   */
  char gcCause[80];        /*!< String about GC casue.                */
  jlong FGCCount;          /*!< Full-GC count.                        */
  jlong YGCCount;          /*!< Young-GC count.                       */
  jlong gcWorktime;        /*!< GC worktime.                          */
  jlong newAreaSize;       /*!< New area using size.                  */
  jlong oldAreaSize;       /*!< Old area using size.                  */
  jlong totalHeapSize;     /*!< Total heap size.                      */
  jlong metaspaceUsage;    /*!< Usage of PermGen or Metaspace.        */
  jlong metaspaceCapacity; /*!< Max capacity of PermGen or Metaspace. */
} TSnapShotFileHeader;
#pragma pack(pop)

/*!
 * \brief This class is stored class object usage on heap.
 */
class TSnapShotContainer;

/*!
 * \brief This type is for queue store snapshot information.
 */
typedef tbb::concurrent_bounded_queue<TSnapShotContainer *> TSnapShotQueue;


/*!
 * \brief This class is stored class object usage on heap.
 */
class TSnapShotContainer {
 public:
  /*!
   * \brief Initialize snapshot caontainer class.
   * \return Is process succeed.
   * \warning Please call only once from main thread.
   */
  static bool globalInitialize(void);
  /*!
   * \brief Finalize snapshot caontainer class.
   * \warning Please call only once from main thread.
   */
  static void globalFinalize(void);

  /*!
   * \brief Get snapshot container instance.
   * \return Snapshot container instance.
   * \warning Don't deallocate instance getting by this function.<br>
   *          Please call "releaseInstance" method.
   */
  static TSnapShotContainer *getInstance(void);
  /*!
   * \brief Release snapshot container instance..
   * \param instance [in] Snapshot container instance.
   * \warning Don't access instance after called this function.
   */
  static void releaseInstance(TSnapShotContainer *instance);

  /*!
   * \brief Get class entries count.
   * \return Entries count of class information.
   */
  inline size_t getContainerSize(void) { return this->_header.size; }

  /*!
   * \brief Set time of take snapshot.
   * \param t [in] Datetime of take snapshot.
   */
  inline void setSnapShotTime(jlong t) { this->_header.snapShotTime = t; }

  /*!
   * \brief Set snapshot cause.
   * \param cause [in] Cause of snapshot.
   */
  inline void setSnapShotCause(TInvokeCause cause) {
    this->_header.cause = cause;
  }

  /*!
   * \brief Set total size of Java Heap.
   * \param size [in] Total size of Java Heap.
   */
  inline void setTotalSize(jlong size) { this->_header.totalHeapSize = size; }

  /*!
   * \brief Set JVM performance info to header.
   * \param info [in] JVM running performance information.
   */
  void setJvmInfo(TJvmInfo *info);

  /*!
   * \brief Get snapshot header.
   * \return Snapshot header.
   */
  inline TSnapShotFileHeader *getHeader(void) {
    return (TSnapShotFileHeader *)&this->_header;
  }

  /*!
   * \brief Increment instance count and using size atomically.
   * \param counter [in] Increment target class.
   * \param size    [in] Increment object size.
   */
  void Inc(TObjectCounter *counter, jlong size);

  /*!
   * \brief Increment instance count and using size.
   * \param counter [in] Increment target class.
   * \param operand [in] Right-hand operand (SRC operand).
   *                     This value must be aligned 16bytes.
   */
  void addInc(TObjectCounter *counter, TObjectCounter *operand);

  /*!
   * \brief Find class data.
   * \param objData [in] Class key object.
   * \return Found class data.
   *         Value is null, if class is not found.
   */
  inline TClassCounter *findClass(TObjectData *objData) {
    auto it = counterMap.find(objData);
    return (it != counterMap.end()) ? it->second : NULL;
  }

  /*!
   * \brief Find child class data.
   * \param clsCounter [in] Parent class counter object.
   * \param klassOop   [in] Child class key object.
   * \return Found class data.
   *         Value is null, if class is not found.
   */
  inline TChildClassCounter *findChildClass(TClassCounter *clsCounter,
                                            void *klassOop) {
    TChildClassCounter *prevCounter = NULL;
    TChildClassCounter *morePrevCounter = NULL;
    TChildClassCounter *counter = clsCounter->child;

    if (counter == NULL) {
      return NULL;
    }

    /* Search children class list. */
    while (counter->objData->klassOop != klassOop) {
      morePrevCounter = prevCounter;
      prevCounter = counter;
      counter = counter->next;

      if (counter == NULL) {
        return NULL;
      }
    }

    /* LFU (Least Frequently Used). */
    if (counter != NULL) {
      counter->callCount++;

      /* If counter need move to list head. */
      if (prevCounter != NULL && prevCounter->callCount <= counter->callCount) {
        prevCounter->next = counter->next;
        if (morePrevCounter != NULL) {
          /* Move to near list head. */
          morePrevCounter->next = counter;
        } else {
          /* Move list head. */
          clsCounter->child = counter;
        }
        counter->next = prevCounter;
      }
    }
    return counter;
  }

  /*!
   * \brief Append new-class to container.
   * \param objData [in] New-class key object.
   * \return New-class data.
   */
  virtual TClassCounter *pushNewClass(TObjectData *objData);

  /*!
   * \brief Append new-child-class to container.
   * \param clsCounter [in] Parent class counter object.
   * \param objData    [in] New-child-class key object.
   * \return New-class data.
   */
  virtual TChildClassCounter *pushNewChildClass(TClassCounter *clsCounter,
                                                TObjectData *objData);

  /*!
   * \brief Output GC statistics information.
   */
  void printGCInfo(void);

  /*!
   * \brief Clear snapshot data.
   */
  void clear(bool isForce);

  /*!
   * \brief Set "isCleared" flag.
   */
  inline void setIsCleared(bool flag) { this->isCleared = flag; }

 protected:
  /*!
   * \brief TSnapshotContainer constructor.
   */
  TSnapShotContainer(void);
  /*!
   * \brief TSnapshotContainer destructor.
   */
  virtual ~TSnapShotContainer(void);

  /*!
   * \brief Zero clear to TObjectCounter.
   * \param counter TObjectCounter to clear.
   */
  void clearObjectCounter(TObjectCounter *counter);

  /*!
   * \brief Zero clear to TClassCounter.
   * \param counter TClassCounter to clear.
   */
  void clearClassCounter(TClassCounter *counter);

  /*!
   * \brief Zero clear to TClassCounter and its children counter.
   * \param counter TClassCounter to clear.
   */
  void clearChildClassCounters(TClassCounter *counter);

  /*!
   * \brief Snapshot container instance stock queue.
   */
  static TSnapShotQueue *stockQueue;

  /*!
   * \brief Max limit count of snapshot container instance.
   */
  const static unsigned int MAX_STOCK_COUNT = 2;

  /*!
   * \brief Maps of counter of each java class.
   */
  tbb::concurrent_unordered_map<TObjectData *, TClassCounter *,
                                       TNumericalHasher<void *> > counterMap;

  RELEASE_ONLY(private :)

  /*!
   * \brief Snapshot header.
   */
  volatile TSnapShotFileHeader _header;

  /*!
   * \brief Is this container is cleared ?
   */
  volatile bool isCleared;
};

/* Include optimized inline functions. */
#if PROCESSOR_ARCH == X86
#include "arch/x86/snapShotContainer.inline.hpp"
#elif PROCESSOR_ARCH == ARM
#include "arch/arm/snapShotContainer.inline.hpp"
#endif

#ifdef AVX
#include "arch/x86/avx/snapShotContainer.inline.hpp"
#elif defined SSE4
#include "arch/x86/sse4/snapShotContainer.inline.hpp"
#elif defined SSE2 || defined SSE3
#include "arch/x86/sse2/snapShotContainer.inline.hpp"
#elif defined NEON
#include "arch/arm/neon/snapShotContainer.inline.hpp"
#else

/*!
 * \brief Increment instance count and using size.
 * \param counter [in] Increment target class.
 * \param operand [in] Right-hand operand (SRC operand).
 *                     This value must be aligned 16bytes.
 */
inline void TSnapShotContainer::addInc(TObjectCounter *counter,
                                       TObjectCounter *operand) {
  counter->count += operand->count;
  counter->total_size += operand->total_size;
}

/*!
 * \brief Zero clear to TObjectCounter.
 * \param counter TObjectCounter to clear.
 */
inline void TSnapShotContainer::clearObjectCounter(TObjectCounter *counter) {
  counter->count = 0;
  counter->total_size = 0;
}

/*!
 * \brief Zero clear to TClassCounter and its children counter.
 * \param counter TClassCounter to clear.
 */
inline void TSnapShotContainer::clearChildClassCounters(
    TClassCounter *counter) {
  /* Reset counter of children class. */
  TChildClassCounter *child_counter = counter->child;
  while (child_counter != NULL) {
    child_counter->counter->count = 0;
    child_counter->counter->total_size = 0;

    // child_counter->callCount >>= 1;
    child_counter = child_counter->next;
  }

  /* Reset counter of all class. */
  this->clearObjectCounter(counter->counter);
}

#endif

#endif  // _SNAPSHOT_CONTAINER_HPP
