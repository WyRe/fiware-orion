/*
*
* Copyright 2016 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Ken Zangelin
*/
#include <sys/time.h>
#include <string>
#include <map>

#include "logMsg/logMsg.h"
#include "logMsg/traceLevels.h"

#include "metricsMgr/MetricsManager.h"
#include "common/JsonHelper.h"



/* ****************************************************************************
*
* MetricsManager::MetricsManager -
*/
MetricsManager::MetricsManager(): on(false), semWaitStatistics(false), semWaitTime(0)
{
}



/* ****************************************************************************
*
* MetricsManager::init -
*
* NOTE
*   The semaphore is created even though the metrics manager is not turned on.
*   It's only one sys-call, and this way, the broker is prepared to receive 'on/off'
*   via REST.
*/
bool MetricsManager::init(bool _on, bool _semWaitStatistics)
{
  on                 = _on;
  semWaitStatistics  = _semWaitStatistics;

  if (sem_init(&sem, 0, 1) == -1)
  {
    LM_E(("Runtime Error (error initializing 'metrics mgr' semaphore: %s)", strerror(errno)));
    return false;
  }

  return true;
}



/* ****************************************************************************
*
* MetricsManager::semTake - 
*/
void MetricsManager::semTake(void)
{
  if (semWaitStatistics)
  {
    struct timeval start;
    struct timeval end;

    gettimeofday(&start, NULL);
    sem_wait(&sem);
    gettimeofday(&end, NULL);

    // Add semaphore waiting time to the accumulator (semWaitTime is in microseconds)
    semWaitTime += (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
  }
  else
  {
    sem_wait(&sem);
  }
}



/* ****************************************************************************
*
* MetricsManager::semGive - 
*/
void MetricsManager::semGive(void)
{
  sem_post(&sem);
}



/* ****************************************************************************
*
* MetricsManager::semWaitTimeGet - 
*/
long long MetricsManager::semWaitTimeGet(void)
{
  return semWaitTime;
}



/* ****************************************************************************
*
* MetricsManager::add -
*/
void MetricsManager::add(const std::string& srv, const std::string& subServ, const std::string& metric, int value)
{
  if (on == false)
  {
    return;
  }

  semTake();

  // Do we have the service in the map?
  if (metrics.find(srv) == metrics.end())
  {
    // not found: create it
    metrics[srv] = new std::map<std::string, std::map<std::string, int>*>;
  }

  // Do we have the subservice in the map?
  if (metrics[srv]->find(subServ) == metrics[srv]->end())
  {
    //
    // not found: create it
    // FIXME PR: this syntax should be simpler, closer to
    // metrics[srv][subServ] = new std::map<std::string, int>;
    //
    metrics[srv]->insert(std::pair<std::string, std::map<std::string, int>*>(subServ, new std::map<std::string, int>));
  }

  // Do we have the metric in the map?
  if (metrics[srv]->at(subServ)->find(metric) == metrics[srv]->at(subServ)->end())
  {
    //
    // not found: create it
    // FIXME PR: I don't like the at() and pair() syntax, I'd prefer a syntax closer to:
    // metrics[srv][subServ][metric] = 0;
    //
    metrics[srv]->at(subServ)->insert(std::pair<std::string, int>(metric, 0));
  }

  metrics[srv]->at(subServ)->at(metric) += value;

  semGive();
}



/* ****************************************************************************
*
* MetricsManager::reset -
*/
void MetricsManager::reset(void)
{
  if (on == false)
  {
    return;
  }

  semTake();
  // FIXME PR (see .h)
  semGive();
}



/* ****************************************************************************
*
* MetricsManager::toJson -
*
* FIXME PR: needs a refactor (see .h)
*/
std::string MetricsManager::toJson(void)
{
  if (on == false)
  {
    return "";
  }

  semTake();

  //
  // Three iterators needed to iterate over the 'triple-map' metrics:
  //   serviceIter      to iterate over all services
  //   subServiceIter   to iterate over all sub-services of a service
  //   metricIter       to iterate over all metrics of a sub-service
  //
  std::map<std::string, std::map<std::string, std::map<std::string, int>*>*>::iterator  serviceIter;
  std::map<std::string, std::map<std::string, int>*>::iterator                          subServiceIter;
  std::map<std::string, int>::iterator                                                  metricIter;
  JsonHelper                                                                            top;
  JsonHelper                                                                            services;

  for (serviceIter = metrics.begin(); serviceIter != metrics.end(); ++serviceIter)
  {
    JsonHelper                                          subServiceTop;
    JsonHelper                                          jhSubService;
    std::string                                         service        = serviceIter->first;
    std::map<std::string, std::map<std::string, int>*>* servMap        = serviceIter->second;

    for (subServiceIter = servMap->begin(); subServiceIter != servMap->end(); ++subServiceIter)
    {
      JsonHelper                  jhMetrics;
      std::string                 subServ    = subServiceIter->first;
      std::map<std::string, int>* metricMap  = subServiceIter->second;

      for (metricIter = metricMap->begin(); metricIter != metricMap->end(); ++metricIter)
      {
        std::string  metric = metricIter->first;
        int          value  = metricIter->second;

        jhMetrics.addNumber(metric, value);
      }

      jhSubService.addRaw(subServ, jhMetrics.str());
    }

    subServiceTop.addRaw("subservs", jhSubService.str());
    services.addRaw(service, subServiceTop.str());
  }

  top.addRaw("services", services.str());
  semGive();
  return top.str();
}



/* ****************************************************************************
*
* isOn - 
*/
bool MetricsManager::isOn(void)
{
  return on;
}



/* ****************************************************************************
*
* MetricsManager::semStateGet - 
*/
const char* MetricsManager::semStateGet(void)
{
  int value;

  if (sem_getvalue(&sem, &value) == -1)
  {
    return "error";
  }

  if (value == 0)
  {
    return "taken";
  }

  return "free";  
}