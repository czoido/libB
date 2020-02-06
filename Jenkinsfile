def artifactory_name = "Artifactory Docker"
def artifactory_repo = "hackathonv5-build"
def docker_runs = [:]  // [id] = [docker_image, profile]

docker_runs["conanio-gcc8"] = ["conanio/gcc8", "linux_gcc_8_x86_64"]
docker_runs["conanio-gcc7"] = ["conanio/gcc7", "linux_gcc_7_x86_64"]
// Conan arm docker images are not updated so we can not use conan_build_info --v
// docker_runs["conanio-gcc8-armv7hf"] = ["conanio/gcc8-armv7hf", "linux_gcc_8_armv7hf"]
// docker_runs["conanio-gcc7-armv7hf"] = ["conanio/gcc7-armv7hf", "linux_gcc_7_armv7hf"]

// temporary
docker_runs["conanio-gcc8_temp"] = ["conanio/gcc8", "conanio-gcc8"]	
docker_runs["conanio-gcc7_temp"] = ["conanio/gcc7", "conanio-gcc7"]

ArrayList line_split(String text) {
  return text.split('\\r?\\n').findAll{it.size() > 0} as ArrayList
}

def organization = "demo-ci-conan"
def user_channel = "demo/testing"
def config_url = "https://github.com/demo-ci-conan/settings.git"
def projects = line_split(readTrusted('dependent-projects.txt')).collect { "${it}@${user_channel}" } // TODO: Get list dynamically

String reference_revision = null
String repository = null
String sha1 = null

def shell_quote(word) {
  return "'" + word.replace("'", "'\\''") + "'"
}

def get_stages(id, docker_image, artifactory_name, artifactory_repo, profile, user_channel, config_url) {
    return {
      stage(id) {
        node {
            docker.image(docker_image).inside("--net=host") {
                def scmVars = checkout scm
                def repository = scmVars.GIT_URL.tokenize('/')[3].split("\\.")[0]
                withEnv(["CONAN_USER_HOME=${env.WORKSPACE}/conan_cache"]) {
                    def server = Artifactory.server artifactory_name
                    def client = Artifactory.newConanClient(userHome: "${env.WORKSPACE}/conan_cache".toString())
                    def remoteName = "artifactory-local"
                    def lockfile = "${id}.lock"
                    try {
                        echo("Start build info")
                        def artifactoryBuildInfo = Artifactory.newBuildInfo()
                        sh "conan_build_info --v2 start ${shell_quote(artifactoryBuildInfo.getName())} ${shell_quote(artifactoryBuildInfo.getNumber())}"

                        client.run(command: "config install ${config_url}".toString())
                        client.remote.add server: server, repo: artifactory_repo, remoteName: remoteName, force: true

                        echo "Running ${id} in ${docker_image}"

                        echo("Get dependencies and create app")
                        String arguments = "--profile ${profile} --lockfile=${lockfile}"
                        client.run(command: "graph lock . ${arguments}".toString())
                        client.run(command: "create . ${user_channel} ${arguments} --build missing --ignore-dirty".toString())
                        sh "cat ${shell_quote(lockfile)}"

                        name = sh (script: "conan inspect . --raw name", returnStdout: true).trim()
                        version = sh (script: "conan inspect . --raw version", returnStdout: true).trim()

                        def lockfile_url = "${server.url}/hackathonv5-metadata/${name}/${version}@${user_channel}/${profile}/conan.lock"
                        def lockfile_sha1 = sha1(file: lockfile)
                        withCredentials([usernamePassword(credentialsId: 'hack-tt-artifactory', usernameVariable: 'CONAN_LOGIN_USERNAME', passwordVariable: 'CONAN_PASSWORD')]) {
                          sh "curl --user \"\${CONAN_LOGIN_USERNAME}\":\"\${CONAN_PASSWORD}\" --header 'X-Checksum-Sha1:'${shell_quote(lockfile_sha1)} --header 'Content-Type: application/json' ${shell_quote(lockfile_url)} --upload-file ${shell_quote(lockfile)}"
                        }

                        if (id=="conanio-gcc8") {// TODO fix this
                            echo("Calculate full reference")
                            def search_output = "search_output.json"
                            sh("""\
conan search ${name}/${version}@${user_channel} --revisions --raw --json=${search_output}
cat search_output.json
""")
                            stash name: 'full_reference', includes: 'search_output.json'
                        }

                        echo("Upload packages")
                        client.run(command: "upload '*' --all -r ${remoteName} --confirm  --force".toString())

                        echo("Create build info")
                        def buildInfoFilename = "${id}.json"
                        client.run(command: "search *".toString())
                        withCredentials([usernamePassword(credentialsId: 'hack-tt-artifactory', usernameVariable: 'CONAN_LOGIN_USERNAME', passwordVariable: 'CONAN_PASSWORD')]) {
                          sh "conan_build_info --v2 create --lockfile ${lockfile} --user \"\${CONAN_LOGIN_USERNAME}\" --password \"\${CONAN_PASSWORD}\" ${buildInfoFilename}"
                        }
                        // Work around conan_build_info wrongly "escaping" colons (:) with backslashes in the build name
                        def buildInfo = readJSON(file: buildInfoFilename)
                        buildInfo['name'] = buildInfo['name'].replace('\\:', ':')
                        buildInfo['modules'][0]['artifacts'].add([
                          sha1: lockfile_sha1,
                          name: lockfile_url.tokenize('/')[-1],
                        ])

                        buildInfo['vcs'] = [[revision: scmVars.GIT_COMMIT, url: scmVars.GIT_URL]]

                        return buildInfo
                    }
                    finally {
                        deleteDir()
                    }
                }
            }
        }
      }
    }
}


pipeline {
  agent none

  stages {
    stage("Build + upload") {
      steps {
        script {
          docker_runs = withEnv(["CONAN_HOOK_ERROR_LEVEL=40"]) {
            parallel docker_runs.collectEntries { id, values ->
              def (docker_image, profile) = values
                ["${id}": get_stages(id, docker_image, artifactory_name, artifactory_repo, profile, user_channel, config_url)]
            }
          }
        }
      }
    }

    stage("Retrieve and publish build info") {
      agent any

      steps {
        script {
          docker.image("conanio/gcc8").inside("--net=host") {
            def server = Artifactory.server artifactory_name
              def last_info = ""
              docker_runs.each { id, buildInfo ->
                writeJSON file: "${id}.json", json: buildInfo
                if (last_info != "") {
                  sh "conan_build_info --v2 update ${id}.json ${last_info} --output-file mergedbuildinfo.json"
                }
                last_info = "${id}.json"
                sha1 = buildInfo['vcs'][0]['revision']
                repository = buildInfo['vcs'][0]['url'].tokenize('/')[3].split("\\.")[0]
              }
              def buildInfo = readJSON(file: 'mergedbuildinfo.json')
              buildInfo['agent'] = [
                  name: 'Jenkins',
                  version: Jenkins.version as String,
                ]
              buildInfo['url'] = BUILD_URL
              def started = new Date(currentBuild.startTimeInMillis)
              buildInfo['started'] = started.format('yyyy-MM-dd\'T\'HH:mm:ss.SSSXXX')
              buildInfo['durationMillis'] = currentBuild.duration
              def userCauses = currentBuild.buildCauses.find { cause ->
                cause._class.contains('UserIdCause')
              }
              buildInfo['principal'] = userCauses != null ? userCauses.userId : 'anonymous'
              writeJSON(file: 'mergedbuildinfo.json', json: buildInfo)
            unstash 'full_reference'
            def props = readJSON file: "search_output.json"
            reference_revision = props[0]['revision']
            withCredentials([usernamePassword(credentialsId: 'hack-tt-artifactory', usernameVariable: 'CONAN_LOGIN_USERNAME', passwordVariable: 'CONAN_PASSWORD')]) {
              sh """\
                cat mergedbuildinfo.json
                conan_build_info --v2 publish --url ${server.url} --user \"\${CONAN_LOGIN_USERNAME}\" --password \"\${CONAN_PASSWORD}\" mergedbuildinfo.json
                """
            }
          }
        }
      }

      post {
        always {
          deleteDir()
        }
      }
    }

    stage("Trigger dependents jobs") {
      steps {
        script {
          assert reference_revision != null
          assert repository != null
          assert sha1 != null
          def reference = "${name}/${version}@${user_channel}#${reference_revision}"
          echo "Full reference: '${reference}'"

          parallel projects.collectEntries {project_id -> 
            ["${project_id}": {
              build(job: "${currentBuild.fullProjectName.tokenize('/')[0]}/jenkins/master", propagate: true, parameters: [
                  [$class: 'StringParameterValue', name: 'reference',    value: reference   ],
                  [$class: 'StringParameterValue', name: 'project_id',   value: project_id  ],
                  [$class: 'StringParameterValue', name: 'organization', value: organization],
                  [$class: 'StringParameterValue', name: 'repository',   value: repository  ],
                  [$class: 'StringParameterValue', name: 'sha1',         value: sha1        ],
              ])
            }]
          }
        }
      }
    }
  }
}
