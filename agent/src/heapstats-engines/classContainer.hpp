/*!
 * \file classContainer.hpp
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

#ifndef CLASS_CONTAINER_HPP
#define CLASS_CONTAINER_HPP

#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>

#include "snapShotContainer.hpp"
#include "sorter.hpp"
#include "trapSender.hpp"

#if PROCESSOR_ARCH == X86
#include "arch/x86/lock.inline.hpp"
#elif PROCESSOR_ARCH == ARM
#include "arch/arm/lock.inline.hpp"
#endif

/*!
 * \brief This structure stored size of a class used in heap.
 */
typedef struct {
  jlong tag;   /*!< Pointer of TObjectData.                */
  jlong usage; /*!< Class using total size.                */
  jlong delta; /*!< Class delta size from before snapshot. */
} THeapDelta;

typedef tbb::concurrent_unordered_map<void *, TObjectData *,
                                TNumericalHasher<void *> > TClassMap;

/*!
 * \brief Memory usage alert types.
 */
typedef enum { ALERT_JAVA_HEAP, ALERT_METASPACE } TMemoryUsageAlertType;

/*!
 * \brief This type is for storing unloaded class information.
 */
typedef tbb::concurrent_queue<TObjectData *> TClassInfoQueue;

/*!
 * \brief This class is stored class information.<br>
 *        e.g. class-name, class instance count, size, etc...
 */
class TClassContainer {
 public:
  /*!
   * \brief TClassContainer constructor.
   * \param needToClr [in] Flag of deallocate all data on destructor.
   */
  TClassContainer(bool needToClr = true);
  /*!
   * \brief TClassContainer destructor.
   */
  virtual ~TClassContainer(void);

  /*!
   * \brief Append new-class to container.
   * \param klassOop [in] New class oop.
   * \return New-class data.
   */
  virtual TObjectData *pushNewClass(void *klassOop);

  /*!
   * \brief Append new-class to container.
   * \param klassOop [in] New class oop.
   * \param objData  [in] Add new class data.
   * \return New-class data.<br />
   *         This value isn't equal param "objData",
   *         if already registered equivalence class.
   */
  virtual TObjectData *pushNewClass(void *klassOop, TObjectData *objData);

  /*!
   * \brief Mark class in container to remove class.
   * \param target [in] Remove class data.
   */
  virtual void popClass(TObjectData *target);

  /*!
   * \brief Remove class from container.
   * \param target [in] Remove class data.
   */
  virtual void removeClass(TObjectData *target);

  /*!
   * \brief Search class from container.
   * \param klassOop [in] Target class oop.
   * \return Class data of target class.
   */
  inline TObjectData *findClass(void *klassOop) {
    auto itr = classMap->find(klassOop);
    return (itr != classMap->end()) ? itr->second : NULL;
  }

  /*!
   * \brief Update class oop.
   * \param oldKlassOop [in] Target old class oop.
   * \param newKlassOop [in] Target new class oop.
   * \return Class data of target class.
   */
  inline void updateClass(void *oldKlassOop, void *newKlassOop) {
    (*classMap)[newKlassOop] = (*classMap)[oldKlassOop];

    // Class relocation will occur at single-threaded safepoint (not MT).
    // So we can execute unsafe operation.
    classMap->unsafe_erase(oldKlassOop);
  }

  /*!
   * \brief Remove all-class from container.
   */
  virtual void allClear(void);
  /*!
   * \brief Output all-class information to file.
   * \param snapshot [in]  Snapshot instance.
   * \param rank     [out] Sorted-class information.
   * \return Value is zero, if process is succeed.<br />
   *         Value is error number a.k.a. "errno", if process is failure.
   */
  virtual int afterTakeSnapShot(TSnapShotContainer *snapshot,
                                TSorter<THeapDelta> **rank);

  /*!
   * \brief Commit class information changing in class container.<br>
   *        This function needs to prevent the crash which is related
   *        to class unloading. <br>
   *        Agent have to keep ObjectData structure(s) until dumping
   *        SnapShot and showing heap ranking.
   */
  virtual void commitClassChange(void);

 protected:

  RELEASE_ONLY(private :)
  /*!
   * \brief SNMP trap sender.
   */
  TTrapSender *pSender;

  /*!
   * \brief Maps of class counting record.
   */
  TClassMap *classMap;

  /*!
   * \brief Do we need to clear at destructor?
   */
  bool needToClear;

  /*!
   * \brief List of class information which detected unloading.
   */
  TClassInfoQueue *unloadedList;
};

#endif  // CLASS_CONTAINER_HPP
