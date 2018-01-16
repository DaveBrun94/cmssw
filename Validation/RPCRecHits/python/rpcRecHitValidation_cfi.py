import FWCore.ParameterSet.Config as cms

rpcRecHitV = DQMStep1Module('RPCRecHitValid',
    subDir = cms.string("RPC/RPCRecHitV/SimVsReco"),
    simHit = cms.InputTag("g4SimHits", "MuonRPCHits"),
    recHit = cms.InputTag("rpcRecHits"),
    simTrack = cms.InputTag("mix", "MergedTrackTruth"),
    simHitAssoc = cms.InputTag("simHitTPAssocProducer"),
    muon = cms.InputTag("muons"),
)

rpcRecHitValidation_step = cms.Sequence(rpcRecHitV)

from Configuration.Eras.Modifier_fastSim_cff import fastSim
fastSim.toModify(rpcRecHitV, simHit = "MuonSimHits:MuonRPCHits")

