"""Finite current-original specialty cohort, distinct from recovered history."""
from .nhart_legacy_art_lineage import _digest, _source_entries, legacy_specialty_family

CURRENT_ORIGINAL_COHORT = 'current-original-ordinary-specialties-v1'
# Exact selected records, including origin, master, approval and source entries.
# These fingerprints authorize neither arbitrary future records nor Final art.
CURRENT_ORIGINAL_SPECIALTIES = {
    'core:valeska': '5285b3823a077c0c6118c61281defec7a1dcc916f9097b46495a13f87b6df2ef',
    'core:wystan': '57867604432bca6ce12a7e4d0a13dfc1fd493de9732dd542c04d773c0c191953',
    'core:dace': '4a1e4fe52f3195abf6c9dbc48e68dfec46f4c708e936023aabc8aea82edc428c',
    'core:thunar': '6026aff2e633ff9cfb7b268e802dcc095f6581e18951a723d6380998d0190361',
    'core:erdamon': '4debde3a899a344340acc2d39f128ecad284fafd38a15df8ba1860b6c9d0d9cd',
    'core:clancy': 'da08bd5d1bbdebd4f39839562e6c4cda678273f9df9324f356a305370cd75acc',
}

CURRENT_THIRTEEN_COHORT = 'current-original-ordinary-specialties-thirteen-v1'
# Independently selected thirteen-record cohort; the earlier six remain exact.
CURRENT_THIRTEEN_SPECIALTIES = {
    'core:ryland': '95788899b74604866aa8062c9ce218a4e19a30cc550291ef2b665abd496f8a03',
    'core:charna': '77e03040bf37728a7084c27185327877d46bc49d9068595b9c9d616c1f5e970a',
    'core:galthran': '703cbebeaabb722b02c73bc4390796d1c46c6f76733283cc365d50e9c2632c51',
    'core:shakti': '215972dffe4c1c3ea77991b8eee3f861e6a9df134656d0099be1cf8d1a02bd96',
    'core:fiona': '09668621278e14d694a0f736bd42c25033d386b91ffdc44a327edf7dd0b9b4a6',
    'core:vokial': 'c3071817c27ed42d51f0a4019a56b82efbf6705ee2d8cba94261bf0c0847130a',
    'core:piquedram': 'dbe9076a4951d696a5f21d90fee6fc60159402b10125ca32889009ba7036b0df',
    'core:vey': '1d938667c2d35a26210198a82f9adbda1877416e36e977ac91085b50be65aa98',
    'core:lacus': '25da58d28451acf08a2d0df4e75c612c63a770e350d30cfaeb5330a03fc6cfca',
    'core:broghild': '88e716a1677fdd0f30803ead2fc13d157932884ef53480646c2897a06fa1073f',
    'core:gretchin': 'ae43f509a46398cb62f1d6d579b9f5fa2b59973b137c03e4c8074edad74f5630',
    'core:aeris': 'ffd0677351e56106d90b095c1a0c20f73d0ee1391b807e8d330a8b3abaf19fd1',
    'core:theodorus': '1c4bc162f3ef505f51f1a2a55461611af0946a76a3145d4b02543bf657dc8166',
}

CURRENT_FOUR_COHORT = 'current-original-ordinary-specialties-four-v1'
# Exact selected next-four records; no changes to the earlier finite cohorts.
CURRENT_FOUR_SPECIALTIES = {
    'core:pasis': '74c09dea8c29316a80c2394a82726b365172b4ef76afb907e93a3973ac48fe9c',
    'core:kalt': '546865ac09da1bc1dbb2259572bff13c51a47c8f4a16b5f7bdbbe19ada2bb67c',
    'core:korbac': 'd35ad20e68bd2f23ff1aa34ebef1adda895819c9d27a86a4c4dc34ab3adc523e',
    'core:tyraxor': '509037a15a8b2f3bd021a4f462813349d4429fec006ae7c119ae48879215499a',
}


CURRENT_RANGED_PAIR_COHORT = 'current-original-ordinary-specialties-ranged-pair-v1'
# Exact selected two-record cohort; all earlier cohorts retain their meaning.
CURRENT_RANGED_PAIR_SPECIALTIES = {
    'core:christian': '6062f3fdd4a725665b48a7e8a270e934e815d9194c049e9f59a12f3d8a84b0a1',
    'core:jabarkas': '48116cdc1c06b3f27039a71d8d72dd78bc5b85ce867c9a10cf8013f2bd386192',
}


CURRENT_MAINTENANCE_THREE_COHORT = 'current-original-ordinary-specialties-maintenance-three-v1'
# Exact selected three-record cohort; no general future acceptance.
CURRENT_MAINTENANCE_THREE_SPECIALTIES = {
    'core:gerwulf': '1e1a914efb9796d9057c5db9ae2b711e8d1d1500cb7746969213930e0cd61bb9',
    'core:josephine': '4867b8b408aaa243bfbc658ee463a29a314c844cfb29002c2263700b377aeef7',
    'core:krellion': 'db38eb019118d7f4e2bb8243bbb57791144e8b433bfd3ae88b9661b19563ae42',
}


CURRENT_MACHINE_THREE_COHORT = 'current-original-ordinary-specialties-machine-three-v1'
# Exact selected three-record cohort; no general future acceptance.
CURRENT_MACHINE_THREE_SPECIALTIES = {
    'core:arlach': '9c343f069569cd16e1d90819e564278f2ebb822e7e20a6a5649c7cc38f20a402',
    'core:gurnisson': 'b08c5862c18c61dc6d010995ba1b14dbf3ed4a8965e598cc81c047d82fef2817',
    'core:torosar': 'b4128e0152fddcda22d3fbe77fe8c2d047d6534a67bd9d366a3f61b226b55c4e',
}


CURRENT_REMAINING_NINETEEN_COHORT = 'current-original-ordinary-specialties-remaining-nineteen-v1'
# Exact nineteen prior-selected records; no general future acceptance.
CURRENT_REMAINING_NINETEEN_SPECIALTIES = {
    'core:alkin': '905a295e7434e15200aab109a7b545952495d4db29711f39f31c747ed85ee09b',
    'core:calh': '6698dd73524c567bec73ae46cfcc141c8ea6e96a8a90843d7b8068015c7cbd3b',
    'core:drakon': 'a78815779f65d38a525b5e40181d92916a6991f8e66905d369c535002a776ebe',
    'core:ignissa': '1e528c5d9012ab8614a3afb89506f413ddc8c6cb89cc9865a1c86ec78ec314b0',
    'core:ingham': '384963f1b5b769e930d26960e75465d81840f29a4119d9e8ffbbdf824582febe',
    'core:lorelei': '038a22cc8560731938ee8ffe88ac7867b38d2cae595e98966a8f4ff8fec4a61e',
    'core:marius': '433232723612133635405fb85e191c70cc429ac5148d378c6bbe16853dd0b9b9',
    'core:moandor': '6c74866150c53c9bf1db60747e6e9a8f719fdcc0944f3e53847e162eca311835',
    'core:monere': 'cca90c0699d00704776d3caeb657585926e587191669d8f25ac5a30ac123fe3a',
    'core:nymus': 'df164ddf1cf0f41297834100359ee08dcf882f109450890fa6da6274e6616387',
    'core:pyre': 'e9285b5a118601c366fa6785ab50ad8af16e5a29aeeeb6629b05743248c5ee60',
    'core:rashka': 'c76e08e12e49207b5eeda9b4beb750fcc7837dc1b8853f679366af413e1ddd7f',
    'core:shiva': '81f2c9aaa320aceac30669f372e13361252bb4d0c554720065c642dec1db864f',
    'core:sorsha': '17559be3864d240c57fc091ab1746808431bfb5018151fa4faab4fe5d7a283eb',
    'core:straker': '02c7db6e4bfb733dd26a852f308f5684ffe0b846fd488f19408bfe4b801063a5',
    'core:synca': '6cabcf4562a4d91d10c8cbb887e800cc7635a591bbc3847d084cf8364f5d3dde',
    'core:tyris': 'cde458abe937f12a0d1244ea6a8116be1102bd1081c660977b656bb42fbeb324',
    'core:ufretin': 'cff6dfa9f414ba67ad5f49252fa6f4f251d7a73ae81561191a6c918fc2da0ae3',
    'core:yog': '98384459857a20869a24124bb80da18f2f4d8fd083ac601a680a63079cc67412',
}


def specialty_family(row, manifest):
    """Dispatch explicit current provenance; retain exact legacy validation."""
    if 'provenanceCohort' not in row:
        return legacy_specialty_family(row, manifest)
    cohorts = {CURRENT_ORIGINAL_COHORT: CURRENT_ORIGINAL_SPECIALTIES,
               CURRENT_THIRTEEN_COHORT: CURRENT_THIRTEEN_SPECIALTIES,
               CURRENT_FOUR_COHORT: CURRENT_FOUR_SPECIALTIES,
               CURRENT_RANGED_PAIR_COHORT: CURRENT_RANGED_PAIR_SPECIALTIES,
               CURRENT_MAINTENANCE_THREE_COHORT: CURRENT_MAINTENANCE_THREE_SPECIALTIES,
               CURRENT_MACHINE_THREE_COHORT: CURRENT_MACHINE_THREE_SPECIALTIES,
               CURRENT_REMAINING_NINETEEN_COHORT: CURRENT_REMAINING_NINETEEN_SPECIALTIES}
    if cohorts.get(row.get('provenanceCohort'), {}).get(row.get('id')) != _digest(row):
        raise ValueError('Unreviewed current-original specialty provenance')
    _source_entries(row, manifest)
    return row['family']
