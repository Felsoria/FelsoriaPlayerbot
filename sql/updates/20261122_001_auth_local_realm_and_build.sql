-- Client build metadata from LegionCore 7.3.5, sql/base/auth_database.sql.
-- https://github.com/The-Legion-Preservation-Project/LegionCore-7.3.5/blob/main/sql/base/auth_database.sql
INSERT INTO `build_info`
    (`build`, `majorVersion`, `minorVersion`, `bugfixVersion`, `hotfixVersion`,
     `winAuthSeed`, `win64AuthSeed`, `mac64AuthSeed`)
SELECT 26972, 7, 3, 5, '',
       '797ECC19662DCBD5090A4481173F1D26',
       '6E212DEF6A0124A3D9AD07F5E322F7AE',
       '341CFEFE3D72ACA9A4407DC535DED66A'
WHERE NOT EXISTS (SELECT 1 FROM `build_info` WHERE `build` = 26972);

-- Bootstrap an empty install with the loopback realm and the default worldserver port.
INSERT INTO `realmlist`
    (`id`, `name`, `address`, `localAddress`, `localSubnetMask`, `port`,
     `flag`, `gamebuild`, `Region`, `Battlegroup`)
SELECT 1, 'Felsoria', '127.0.0.1', '127.0.0.1', '255.255.255.0', 8085,
       2, 26972, 1, 1
WHERE NOT EXISTS (SELECT 1 FROM `realmlist`);
