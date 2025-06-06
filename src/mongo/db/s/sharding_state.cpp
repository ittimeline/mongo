/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/db/s/sharding_state.h"

#include "mongo/db/cluster_role.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/s/operation_sharding_state.h"
#include "mongo/db/server_options.h"
#include "mongo/db/transaction_resources.h"
#include "mongo/logv2/log.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/decorable.h"

#include <utility>

#include <boost/none.hpp>
#include <boost/optional/optional.hpp>

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kSharding

namespace mongo {
namespace {

const auto getShardingState = ServiceContext::declareDecoration<boost::optional<ShardingState>>();

}  // namespace

ShardingState::ShardingState(bool inMaintenanceMode) : _inMaintenanceMode(inMaintenanceMode) {
    auto [promise, future] = makePromiseFuture<RecoveredClusterRole>();
    _promise = std::move(promise);
    _future = std::move(future).tapAll(
        [this](auto sw) { _awaitClusterRoleRecoveryPromise.setFrom(std::move(sw)); });
}

ShardingState::~ShardingState() = default;

void ShardingState::create(ServiceContext* serviceContext) {
    auto& shardingState = getShardingState(serviceContext);
    invariant(!shardingState);
    shardingState.emplace(serverGlobalParams.maintenanceMode !=
                          ServerGlobalParams::MaintenanceMode::None);
}

ShardingState* ShardingState::get(ServiceContext* serviceContext) {
    auto& shardingState = getShardingState(serviceContext);
    return shardingState.get_ptr();
}

ShardingState* ShardingState::get(OperationContext* operationContext) {
    return ShardingState::get(operationContext->getServiceContext());
}

bool ShardingState::inMaintenanceMode() const {
    return _inMaintenanceMode;
}

void ShardingState::setRecoveryCompleted(RecoveredClusterRole role) {
    LOGV2(
        22081, "Sharding status of the node recovered successfully", "role"_attr = role.toString());
    stdx::unique_lock<stdx::mutex> ul(_mutex);
    invariant(!_future.isReady());

    _promise.emplaceValue(std::move(role));
}

void ShardingState::setRecoveryFailed(Status failedStatus) {
    invariant(!failedStatus.isOK());
    LOGV2(22082, "Sharding status of the node failed to recover", "error"_attr = failedStatus);

    stdx::unique_lock<stdx::mutex> ul(_mutex);
    invariant(!_future.isReady());

    _promise.setError(
        {ErrorCodes::ManualInterventionRequired,
         str::stream()
             << "This instance's sharding role failed to initialize and will remain in this state "
                "until the process is restarted. In addition some manual intervention might be "
                "required, which would require the node to be started in maintenance mode."
             << causedBy(failedStatus)});
}

SharedSemiFuture<ShardingState::RecoveredClusterRole> ShardingState::awaitClusterRoleRecovery() {
    return _awaitClusterRoleRecoveryPromise.getFuture();
}

boost::optional<ClusterRole> ShardingState::pollClusterRole() const {
    if (!_future.isReady())
        return boost::none;

    return _future.get().role;
}

bool ShardingState::enabled() const {
    if (inMaintenanceMode())
        return false;

    const auto role = pollClusterRole();
    return role && (role->has(ClusterRole::ConfigServer) || role->has(ClusterRole::ShardServer));
}

void ShardingState::assertCanAcceptShardedCommands() const {
    if (pollClusterRole())
        return;

    uasserted(ErrorCodes::ShardingStateNotInitialized,
              "Cannot accept sharding commands if sharding state has not been initialized with a "
              "shardIdentity document");
}

ShardId ShardingState::shardId() const {
    uassert(ErrorCodes::ShardingStateNotInitialized,
            "ShardingState cannot be accessed before it is initialized",
            _future.isReady());

    return _future.get().shardId;
}

OID ShardingState::clusterId() const {
    invariant(_future.isReady());
    const auto& role = _future.get();
    return role.clusterId;
}

std::string ShardingState::RecoveredClusterRole::toString() const {
    return str::stream() << clusterId << " : " << role << " : " << configShardConnectionString
                         << " : " << shardId;
}

ShardingState::ScopedTransitionalShardingState::ScopedTransitionalShardingState(
    ScopedTransitionalShardingState&& other)
    : _lock(std::move(other._lock)), _shardingState(other._shardingState) {
    other._shardingState = nullptr;
}

auto ShardingState::ScopedTransitionalShardingState::getCriticalSectionSignal(
    ShardingMigrationCriticalSection::Operation op) const {
    return _shardingState->_transitionalPhaseCritSec.getSignal(op);
}

auto ShardingState::ScopedTransitionalShardingState::getCriticalSectionReason() const {
    return _shardingState->_transitionalPhaseCritSec.getReason();
}

// The transitional phase contains the state transitions too.
// This means the transitional state begins when we start to enter to phase (first crit sec acquire)
// and ends when we finish to leave the phase (last crit sec release)
bool ShardingState::ScopedTransitionalShardingState::isInTransitionalPhase(
    OperationContext* opCtx) const {
    const auto critSecSignal =
        getCriticalSectionSignal(shard_role_details::getLocker(opCtx)->isWriteLocked()
                                     ? ShardingMigrationCriticalSection::kWrite
                                     : ShardingMigrationCriticalSection::kRead);
    return critSecSignal || _shardingState->_transitionalPhaseVersion.is_initialized();
}

ShardingState::ScopedTransitionalShardingState
ShardingState::ScopedTransitionalShardingState::acquireShared(OperationContext* opCtx) {
    auto shardingState = ShardingState::get(opCtx);
    return ScopedTransitionalShardingState(std::shared_lock(shardingState->_transitionalPhaseMutex),
                                           shardingState);
}

ShardingState::ScopedTransitionalShardingState
ShardingState::ScopedTransitionalShardingState::acquireExclusive(OperationContext* opCtx) {
    auto shardingState = ShardingState::get(opCtx);
    return ScopedTransitionalShardingState(std::unique_lock(shardingState->_transitionalPhaseMutex),
                                           shardingState);
}

void ShardingState::ScopedTransitionalShardingState::checkDbVersionOrThrow(
    OperationContext* opCtx, const DatabaseName& dbName) const {
    // If there is a version attached to the OperationContext, use it as the received version.
    // If there is no database version information on the 'opCtx'. This means that the operation
    // represented by 'opCtx' is unversioned, and the database version is always OK for unversioned
    // operations.
    if (OperationShardingState::isComingFromRouter(opCtx)) {
        const auto version = OperationShardingState::get(opCtx).getDbVersion(dbName);
        if (version) {
            checkDbVersionOrThrow(opCtx, dbName, *version);
        }
    };
}

void ShardingState::ScopedTransitionalShardingState::checkDbVersionOrThrow(
    OperationContext* opCtx,
    const DatabaseName& dbName,
    const DatabaseVersion& receivedVersion) const {
    {
        const auto critSecSignal =
            getCriticalSectionSignal(shard_role_details::getLocker(opCtx)->isWriteLocked()
                                         ? ShardingMigrationCriticalSection::kWrite
                                         : ShardingMigrationCriticalSection::kRead);
        const auto optCritSecReason = getCriticalSectionReason();

        uassert(StaleDbRoutingVersion(dbName, receivedVersion, boost::none, critSecSignal),
                fmt::format("The global sharding state critical section is acquired while checking "
                            "for the database {} with reason: {}",
                            dbName.toStringForErrorMsg(),
                            getCriticalSectionReason()->toString()),
                !critSecSignal);
    }

    tassert(10483400,
            "checkDbVersionOrThrow should be called only if we are in transitional state",
            _shardingState->_transitionalPhaseVersion.is_initialized());

    uassert(StaleDbRoutingVersion(
                dbName, receivedVersion, *(_shardingState->_transitionalPhaseVersion)),
            fmt::format("Database version mismatch: transitional version expected for {}",
                        dbName.toStringForErrorMsg()),
            receivedVersion == *(_shardingState->_transitionalPhaseVersion));
}

ShardingState::ScopedTransitionalShardingState::ScopedTransitionalShardingState(
    LockType lock, ShardingState* shardingState)
    : _shardingState(shardingState) {}

}  // namespace mongo
