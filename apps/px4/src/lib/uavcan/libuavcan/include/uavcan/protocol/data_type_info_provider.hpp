/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#ifndef UAVCAN_PROTOCOL_DATA_TYPE_INFO_PROVIDER_HPP_INCLUDED
#define UAVCAN_PROTOCOL_DATA_TYPE_INFO_PROVIDER_HPP_INCLUDED

#include <uavcan/node/service_server.hpp>
#include <uavcan/util/method_binder.hpp>
#include <uavcan/build_config.hpp>
#include <uavcan/protocol/ComputeAggregateTypeSignature.hpp>
#include <uavcan/protocol/GetDataTypeInfo.hpp>
#include <uavcan/debug.hpp>

namespace uavcan
{
/**
 * This class implements the standard services for data type introspection.
 * Once started it does not require any attention from the application.
 * The user does not need to deal with it directly - it's started by the node class.
 */
class UAVCAN_EXPORT DataTypeInfoProvider : Noncopyable
{
    typedef MethodBinder<DataTypeInfoProvider*,
                         void (DataTypeInfoProvider::*)(const protocol::ComputeAggregateTypeSignature::Request&,
                                                        protocol::ComputeAggregateTypeSignature::Response&)>
        ComputeAggregateTypeSignatureCallback;

    typedef MethodBinder<DataTypeInfoProvider*,
                         void (DataTypeInfoProvider::*)(const protocol::GetDataTypeInfo::Request&,
                                                        protocol::GetDataTypeInfo::Response&)> GetDataTypeInfoCallback;

    ServiceServer<protocol::ComputeAggregateTypeSignature, ComputeAggregateTypeSignatureCallback> cats_srv_;
    ServiceServer<protocol::GetDataTypeInfo, GetDataTypeInfoCallback> gdti_srv_;

    INode& getNode() { return cats_srv_.getNode(); }

    static bool isValidDataTypeKind(DataTypeKind kind)
    {
        return (kind == DataTypeKindMessage) || (kind == DataTypeKindService);
    }

    void handleComputeAggregateTypeSignatureRequest(const protocol::ComputeAggregateTypeSignature::Request& request,
                                                    protocol::ComputeAggregateTypeSignature::Response& response)
    {
        const DataTypeKind kind = DataTypeKind(request.kind.value); // No mapping needed
        if (!isValidDataTypeKind(kind))
        {
            UAVCAN_TRACE("DataTypeInfoProvider", "ComputeAggregateTypeSignature request with invalid DataTypeKind %d",
                         kind);
            return;
        }

        UAVCAN_TRACE("DataTypeInfoProvider", "ComputeAggregateTypeSignature request for dtk=%d, len(known_ids)=%d",
                     int(request.kind.value), int(request.known_ids.size()));

        // Correcting the mask length according to the data type kind
        response.mutually_known_ids = request.known_ids;
        response.mutually_known_ids.resize(
            static_cast<uint16_t>(DataTypeID::getMaxValueForDataTypeKind(kind).get() + 1U));

        response.aggregate_signature =
            GlobalDataTypeRegistry::instance().computeAggregateSignature(kind, response.mutually_known_ids).get();
    }

    void handleGetDataTypeInfoRequest(const protocol::GetDataTypeInfo::Request& request,
                                      protocol::GetDataTypeInfo::Response& response)
    {
        /*
         * Asking the Global Data Type Registry for the matching type descriptor, either by name or by ID
         */
        const DataTypeDescriptor* desc = NULL;

        if (request.name.empty())
        {
            response.id   = request.id;   // Pre-setting the fields so they have meaningful values even in
            response.kind = request.kind; // ...case of failure.

            if (!isValidDataTypeKind(DataTypeKind(request.kind.value)))
            {
                UAVCAN_TRACE("DataTypeInfoProvider", "GetDataTypeInfo request with invalid DataTypeKind %i",
                             static_cast<int>(request.kind.value));
                return;
            }

            desc = GlobalDataTypeRegistry::instance().find(DataTypeKind(request.kind.value), request.id);
        }
        else
        {
            response.name = request.name;

            desc = GlobalDataTypeRegistry::instance().find(request.name.c_str());
        }

        if (desc == NULL)
        {
            UAVCAN_TRACE("DataTypeInfoProvider",
                         "Cannot process GetDataTypeInfo for nonexistent type: dtid=%i dtk=%i name='%s'",
                         static_cast<int>(request.id), static_cast<int>(request.kind.value), request.name.c_str());
            return;
        }

        UAVCAN_TRACE("DataTypeInfoProvider", "GetDataTypeInfo request for %s", desc->toString().c_str());

        /*
         * Filling the response struct
         */
        response.signature  = desc->getSignature().get();
        response.id         = desc->getID().get();
        response.kind.value = desc->getKind();
        response.mask       = protocol::GetDataTypeInfo::Response::MASK_KNOWN;
        response.name       = desc->getFullName();

        const Dispatcher& dispatcher = getNode().getDispatcher();

        if (desc->getKind() == DataTypeKindService)
        {
            if (dispatcher.hasServer(desc->getID().get()))
            {
                response.mask |= protocol::GetDataTypeInfo::Response::MASK_SERVING;
            }
        }
        else if (desc->getKind() == DataTypeKindMessage)
        {
            if (dispatcher.hasSubscriber(desc->getID().get()))
            {
                response.mask |= protocol::GetDataTypeInfo::Response::MASK_SUBSCRIBED;
            }
            if (dispatcher.hasPublisher(desc->getID().get()))
            {
                response.mask |= protocol::GetDataTypeInfo::Response::MASK_PUBLISHING;
            }
        }
        else
        {
            UAVCAN_ASSERT(0); // That means that GDTR somehow found a type of an unknown kind. The horror.
        }
    }

public:
    explicit DataTypeInfoProvider(INode& node)
        : cats_srv_(node)
        , gdti_srv_(node)
    { }

    int start()
    {
        int res = 0;

        res = cats_srv_.start(
            ComputeAggregateTypeSignatureCallback(this, &DataTypeInfoProvider::handleComputeAggregateTypeSignatureRequest));
        if (res < 0)
        {
            goto fail;
        }

        res = gdti_srv_.start(GetDataTypeInfoCallback(this, &DataTypeInfoProvider::handleGetDataTypeInfoRequest));
        if (res < 0)
        {
            goto fail;
        }

        UAVCAN_ASSERT(res >= 0);
        return res;

    fail:
        UAVCAN_ASSERT(res < 0);
        cats_srv_.stop();
        gdti_srv_.stop();
        return res;
    }
};

}

#endif // UAVCAN_PROTOCOL_DATA_TYPE_INFO_PROVIDER_HPP_INCLUDED
