import FWCore.ParameterSet.Config as cms
    
ecalPreshowerFEDIntegrityTask = DQMStep1Module('ESFEDIntegrityTask',
                                               prefixME = cms.untracked.string('EcalPreshower'),
                                               ESDCCCollections = cms.InputTag("ecalPreshowerDigis"),
                                               ESKChipCollections = cms.InputTag("ecalPreshowerDigis"),
                                               FEDRawDataCollection = cms.InputTag("rawDataCollector"),
                                               OutputFile = cms.untracked.string(""),
                                               FEDDirName =cms.untracked.string("FEDIntegrity")
                                               )

