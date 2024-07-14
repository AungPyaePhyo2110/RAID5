#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
// #include <iostream>
using namespace std;

constexpr int SECTOR_SIZE = 512;
constexpr int MAX_RAID_DEVICES = 16;
constexpr int MAX_DEVICE_SECTORS = 1024 * 1024 * 2;
constexpr int MIN_DEVICE_SECTORS = 1 * 1024 * 2;

constexpr int RAID_STOPPED = 0;
constexpr int RAID_OK = 1;
constexpr int RAID_DEGRADED = 2;
constexpr int RAID_FAILED = 3;

struct TBlkDev
{
  int m_Devices;
  int m_Sectors;
  int (*m_Read)(int, int, void *, int);
  int (*m_Write)(int, int, const void *, int);
};
#endif /* __PROGTEST__ */

class CRaidVolume
{
public:
  static bool create(const TBlkDev &dev);
  int start(const TBlkDev &dev);
  int stop();
  int resync();
  int status() const;
  int size() const;
  bool read(int secNr,
            void *data,
            int secCnt);
  bool write(int secNr,
             const void *data,
             int secCnt);
  CRaidVolume();
  ~CRaidVolume();
  bool recoverData(int targetSector, int failedDisk, char *buffer);
  int findProblemDisk();
  int findTrueNumber(int *numbers, int diskCount);

protected:
  // todo
  TBlkDev device;
  int currentStatus;
  int counter;
  bool diskStatus[MAX_RAID_DEVICES];
};

CRaidVolume::CRaidVolume()
{
  currentStatus = RAID_STOPPED;
  counter = 1;
}

CRaidVolume::~CRaidVolume()
{
}
// This function initializes the RAID device.
//  This means that the service records your implementation needs will be written to the underlying disks
//  . This function will be called only once when a new RAID device is being created.
//   The service records may be needed to hold information about the RAID layout and a list of functional disks when the RAID was most recently disassembled (stopped)
//   . The latter information is crucial to correctly rebuild the RAID after an off-line replacement of a non-functional disk.
//    Your implementation is simplified in that it does not have to compute the parity. Initially, the disks are filled with zeros,
//    thus the parity is already correct (this only applies to the create). Note that the call to create does not start the RAID device (i.e., it does not create any CRaidVolume instances).
//     The call has a parameter to communicate with the underlying disks, the return value is a success indicator (true = ok, false = failed)
bool CRaidVolume::create(const TBlkDev &dev)
{
  if (dev.m_Devices < 3 || dev.m_Devices > MAX_RAID_DEVICES || dev.m_Sectors < MIN_DEVICE_SECTORS || dev.m_Sectors > MAX_DEVICE_SECTORS)
    return false;

  int start = 1;
  char serviceRecordData[SECTOR_SIZE];
  memset(serviceRecordData, 0, SECTOR_SIZE);
  memcpy(serviceRecordData, &start, sizeof(int));
  // fill service records
  for (int i = 0; i < dev.m_Devices; i++)
  {
    // service record
    if (dev.m_Write(i, dev.m_Sectors - 1, serviceRecordData, 1) != 1)
      return false;
  }

  return true;
}

int CRaidVolume::findTrueNumber(int *numbers, int diskCount)
{
  int *count = new int[diskCount];
  for (int i = 0; i < diskCount; i++)
  {
    count[i] = 0;
  }

  for (int i = 0; i < diskCount; i++)
  {
    for (int j = 0; j < diskCount; j++)
    {
      if (numbers[i] == numbers[j])
        count[i]++;
    }
  }

  int result = 0;
  int maxCount = 0;
  for (int i = 0; i < diskCount; i++)
  {
    if (count[i] > maxCount)
    {
      result = numbers[i];
      maxCount = count[i];
    }
  }

  delete[] count;
  return result;
}

// This method brings the RAID device on-line.
//  The underlying disks have already been prepared (either by create, or the RAID was previously stopped by stop).
// The function shall read the service records from the disks and assemble the RAID array.
// After the call, the array shall be prepared for read/write requests.
// The status of the array will be either RAID_OK, RAID_DEGRADED, or RAID_FAILED.
// The function shall not start reconstruction of the array (even if the failing disk was replaced).
// The function is similar to mdadm --assemble in Linux SW RAID. The function shall return RAID status (RAID_OK, RAID_DEGRADED, or RAID_FAILED).
int CRaidVolume::start(const TBlkDev &dev)
{
  device = dev;

  int nonWorkingDisks = 0;

  for (int i = 0; i < dev.m_Devices; i++)
    diskStatus[i] = true;

  char buffer[SECTOR_SIZE];
  memset(buffer, 0, SECTOR_SIZE);
  int *numbers = new int[device.m_Devices];
  for (int i = 0; i < device.m_Devices; i++)
  {
    if (device.m_Read(i, device.m_Sectors - 1, buffer, 1) == 1)
    {
      memcpy(&(numbers[i]), buffer, sizeof(int));
    }
    else
    {
      numbers[i] = -1;
      diskStatus[i] = false;
    }
  }

  int trueNumber = findTrueNumber(numbers, device.m_Devices);

  counter = trueNumber;

  for (int i = 0; i < device.m_Devices; i++)
  {
    if (numbers[i] != trueNumber)
    {
      diskStatus[i] = false;
      nonWorkingDisks++;
    }
  }

  // std::cout << "nonWorkingDISK:  " << nonWorkingDisks << std::endl;

  if (nonWorkingDisks == 0)
    currentStatus = RAID_OK;
  else if (nonWorkingDisks == 1)
    currentStatus = RAID_DEGRADED;
  else
  {
    currentStatus = RAID_FAILED;
  }

  delete[] numbers;
  return currentStatus;
}

int CRaidVolume::stop()
{
  counter++;
  char serviceRecordData[SECTOR_SIZE];
  memset(serviceRecordData, 0, SECTOR_SIZE);
  memcpy(serviceRecordData, &counter, sizeof(int));
  for (int i = 0; i < device.m_Devices; i++)
  {
    if (diskStatus[i])
    {
      device.m_Write(i, device.m_Sectors - 1, serviceRecordData, 1);
    }
  }

  currentStatus = RAID_STOPPED;
  return currentStatus;
}

// return false when another disk is failing
bool CRaidVolume::recoverData(int targetSector, int failedDisk, char *buffer)
{
  char readBuffer[SECTOR_SIZE];
  memset(readBuffer, 0, SECTOR_SIZE);
  memset(buffer, 0, SECTOR_SIZE);

  for (int j = 0; j < device.m_Devices; j++)
  {
    if (j != failedDisk)
    {
      if (!diskStatus[j])
        return false;
      if (device.m_Read(j, targetSector, &readBuffer, 1) != 1)
      {
        diskStatus[j] = false;
        return false;
      }
      for (int k = 0; k < SECTOR_SIZE; k++)
      {
        buffer[k] ^= readBuffer[k];
      }
    }
  }
  return true;
}

int CRaidVolume::findProblemDisk()
{
  for (int i = 0; i < device.m_Devices; i++)
  {
    if (!diskStatus[i])
      return i;
  }
  return -1;
}

int CRaidVolume::resync()
{
  if (currentStatus != RAID_DEGRADED)
    return currentStatus;
  int problemDisk = findProblemDisk();

  char recoveryBuffer[SECTOR_SIZE];
  for (int i = 0; i < device.m_Sectors; i++)
  {
    if (!recoverData(i, problemDisk, recoveryBuffer))
    {
      currentStatus = RAID_FAILED;
      return currentStatus;
    }
    if (device.m_Write(problemDisk, i, recoveryBuffer, 1) != 1)
    {
      // std::cout << "SECTOR RECOVER NOT OK - DEGRADED" << std::endl;
      diskStatus[problemDisk] = false;
      return currentStatus;
    }
  }

  // std::cout << "RESYNC SUCCESS" << std::endl;
  diskStatus[problemDisk] = true;
  currentStatus = RAID_OK;
  return currentStatus;
}

int CRaidVolume::status() const
{
  return currentStatus;
}
int CRaidVolume::size() const
{
  return (device.m_Devices - 1) * (device.m_Sectors - 2);
}

bool CRaidVolume::read(int secNr, void *data, int secCnt)
{
  if (currentStatus == RAID_STOPPED)
    return false;
  char *buffer = static_cast<char *>(data);
  // parities are in one column
  int numberOfColumns = device.m_Devices - 1;
  for (int i = 0; i < secCnt; i++)
  {
    int globalSectorNr = secNr + i;
    int rowNumber = globalSectorNr / numberOfColumns;
    int columnNumber = globalSectorNr % numberOfColumns;
    int parityDiskColumnNumber = rowNumber % device.m_Devices;

    // if the column is before the  parity disk - ok
    // if the column is after the parity disk -- move one to right
    int targetDisk = columnNumber < parityDiskColumnNumber ? columnNumber : columnNumber + 1;
    int targetSector = rowNumber;

    if (!diskStatus[targetDisk] && currentStatus == RAID_DEGRADED)
    {
      if (!recoverData(targetSector, targetDisk, buffer + (i * SECTOR_SIZE)))
      {
        currentStatus = RAID_FAILED;
        return false;
      }
    }
    else
    {
      if (device.m_Read(targetDisk, targetSector, buffer + (i * SECTOR_SIZE), 1) != 1)
      {
        diskStatus[targetDisk] = false;
        if (currentStatus == RAID_DEGRADED)
        {
          currentStatus = RAID_FAILED;
          return false;
        }
        else
        {
          currentStatus = RAID_DEGRADED;
          if (!recoverData(targetSector, targetDisk, buffer + (i * SECTOR_SIZE)))
          {
            currentStatus = RAID_FAILED;
            return false;
          }
        }
      }
    }
  }
  return true;
}
bool CRaidVolume::write(int secNr, const void *data, int secCnt)
{
  if (currentStatus == RAID_STOPPED)
    return false;
  int numberOfColumns = device.m_Devices - 1;
  for (int i = 0; i < secCnt; i++)
  {
    int globalSectorNr = secNr + i;
    int rowNumber = globalSectorNr / numberOfColumns;
    int columnNumber = globalSectorNr % numberOfColumns;
    int parityDiskColumnNumber = rowNumber % device.m_Devices;

    // if the column is before the  parity disk - ok
    // if the column is after the parity disk -- move one to right
    int targetDisk = columnNumber < parityDiskColumnNumber ? columnNumber : columnNumber + 1;
    int targetSector = rowNumber;

    char oldData[SECTOR_SIZE];
    memset(oldData, 0, SECTOR_SIZE);
    const char *writeData = static_cast<const char *>(data) + i * SECTOR_SIZE;
    char oldparityData[SECTOR_SIZE];
    memset(oldparityData, 0, SECTOR_SIZE);

    // retrieve old data


    if(!read(globalSectorNr,oldData,1))
      return false;

    // retrieve oldparitydata;
    if (diskStatus[parityDiskColumnNumber])
    {
      if (device.m_Read(parityDiskColumnNumber, targetSector, oldparityData, 1) != 1)
      {
        diskStatus[parityDiskColumnNumber] = false;
        if (currentStatus == RAID_DEGRADED)
        {
          currentStatus = RAID_FAILED;
          return false;
        }
        else
        {
          currentStatus = RAID_DEGRADED;
        }
      }


      // calculate new parity data
      for (int k = 0; k < SECTOR_SIZE; k++)
      {
        oldparityData[k] ^= oldData[k];
        oldparityData[k] ^= writeData[k];
      }

      if (device.m_Write(parityDiskColumnNumber, targetSector, oldparityData, 1) != 1)
      {
        diskStatus[parityDiskColumnNumber] = false;
        if (currentStatus == RAID_DEGRADED)
        {
          currentStatus = RAID_FAILED;
          return false;
        }
        else
        {
          currentStatus = RAID_DEGRADED;
        }
      }

    }



    // write new Data
    if (diskStatus[targetDisk])
    {
      if (device.m_Write(targetDisk, targetSector, writeData, 1) != 1)
      {
        diskStatus[targetDisk] = false;
        if (currentStatus == RAID_DEGRADED)
        {
          currentStatus = RAID_FAILED;
          return false;
        }
        else
        {
          currentStatus = RAID_DEGRADED;
        }
      }
    }
  }
  return true;
}

#ifndef __PROGTEST__
#include "tests2.inc"
#endif /* __PROGTEST__ */